/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/a210.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/mgmt/scmi/clock.h>
#include <zephyr/mgmt/scmi/reset.h>
#include <zephyr/mgmt/scmi/sensor.h>
#include <zephyr/drivers/sensor/mr75203.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/power_domain/a210.h>
#include <zephyr/mgmt/scmi/power.h>
#include <zephyr/pm/device.h>
#include <string.h>
#include "resources.h"

#define USER DT_PATH(zephyr_user)
static const struct device *const clocks = DEVICE_DT_GET(DT_CLOCKS_CTLR_BY_IDX(USER, 0));
static const struct device *const resets = DEVICE_DT_GET(DT_RESET_CTLR_BY_IDX(USER, 0));
static const struct device *const pvt = DEVICE_DT_GET(DT_NODELABEL(pvt));
static const struct device *const system_controller = DEVICE_DT_GET(DT_NODELABEL(a210_system));
static const struct device *const timebase = DEVICE_DT_GET(DT_NODELABEL(aon_timebase));
static uint32_t sensor_enabled;
static uint32_t thermal_sampled;
static uint8_t thermal_hits;

#define CHECK_CLOCK(node, prop, i)                                                                 \
	BUILD_ASSERT(DT_CLOCKS_CELL_BY_IDX(node, i, id) == i);                                     \
	BUILD_ASSERT(DT_SAME_NODE(DT_CLOCKS_CTLR_BY_IDX(node, i), DT_CLOCKS_CTLR_BY_IDX(USER, 0)));
#define CHECK_RESET(node, prop, i)                                                                 \
	BUILD_ASSERT(DT_RESET_ID_BY_IDX(node, i) == i);                                            \
	BUILD_ASSERT(DT_SAME_NODE(DT_RESET_CTLR_BY_IDX(node, i), DT_RESET_CTLR_BY_IDX(USER, 0)));
DT_FOREACH_PROP_ELEM(USER, clocks, CHECK_CLOCK)
DT_FOREACH_PROP_ELEM(USER, resets, CHECK_RESET)

static void numbered_name(char name[16], const char *prefix, unsigned int id)
{
	char digits[5];
	size_t count = 0, pos = strlen(prefix);

	memcpy(name, prefix, pos);
	do {
		digits[count++] = '0' + id % 10;
		id /= 10;
	} while (id && count < sizeof(digits));
	while (count) {
		name[pos++] = digits[--count];
	}
	name[pos] = '\0';
}

static int clock_rate(void *context, uint64_t *rate)
{
	uint32_t hz;
	int ret = clock_control_get_rate(clocks, context, &hz);

	if (!ret) {
		*rate = hz;
	}
	return ret;
}

static int clock_describe(void *context, unsigned int index, uint64_t *rate)
{
	uint32_t hz;
	int ret = a210_clock_describe(clocks, (uintptr_t)context, index, &hz);

	if (!ret) {
		*rate = hz;
	}
	return ret;
}

static int clock_set_rate(void *context, uint64_t rate)
{
	return rate > UINT32_MAX ? -ERANGE
				 : clock_control_set_rate(clocks, context, (void *)(uintptr_t)rate);
}

static int clock_state(void *context, bool *enabled)
{
	enum clock_control_status state = clock_control_get_status(clocks, context);

	if (state != CLOCK_CONTROL_STATUS_ON && state != CLOCK_CONTROL_STATUS_OFF) {
		return -EIO;
	}
	*enabled = state == CLOCK_CONTROL_STATUS_ON;
	return 0;
}

static int clock_enable(void *context, bool enabled)
{
	return enabled ? clock_control_on(clocks, context) : clock_control_off(clocks, context);
}

static const struct scmi_clock_ops clock_ops = {
	.get_rate = clock_rate,
	.describe_rate = clock_describe,
	.set_rate = clock_set_rate,
	.get_enabled = clock_state,
	.set_enabled = clock_enable,
};

static int clock_lookup(void *context, uint32_t id, struct scmi_clock *clock, char name[16])
{
	static const char *const initial_names[] = {
		"uart0",     "uart1",        "uart2",     "uart3",     "uart4",     "uart5",
		"uart6",     "uart7",        "uart8",     "uart9",     "uart0-apb", "uart1-apb",
		"uart2-apb", "uart3-apb",    "uart4-apb", "uart5-apb", "uart6-apb", "uart7-apb",
		"uart8-apb", "uart9-apb",    "peri1-pad", "peri2-pad", "peri3-pad", "aon-sys",
		"aon-i2c1",  "aon-i2c1-apb",
	};
	int count = a210_clock_rate_count(clocks, id);

	ARG_UNUSED(context);
	if (count <= 0 || count > UINT16_MAX) {
		return count < 0 ? count : -ERANGE;
	}
	if (id < ARRAY_SIZE(initial_names)) {
		strcpy(name, initial_names[id]);
	} else {
		numbered_name(name, "a210-clk", id);
	}
	*clock = (struct scmi_clock){
		.name = name,
		.context = (void *)(uintptr_t)id,
		.ops = &clock_ops,
		.rate_count = count,
	};
	return 0;
}

static int reset_set(void *context, bool asserted)
{
	return asserted ? reset_line_assert(resets, (uintptr_t)context)
			: reset_line_deassert(resets, (uintptr_t)context);
}

static int reset_lookup(void *context, uint32_t id, struct scmi_reset_domain *reset, char name[16])
{
	ARG_UNUSED(context);
	numbered_name(name, id < 10 ? "uart" : "a210-rst", id);
	*reset = (struct scmi_reset_domain){
		.name = name,
		.context = (void *)(uintptr_t)id,
		.latency_us = UINT32_MAX,
		.set_asserted = reset_set,
	};
	return 0;
}

static int sensor_read(void *context, int64_t *value)
{
	int32_t milli;
	int ret;

	if (!(sensor_enabled & BIT((uintptr_t)context))) {
		return -EACCES;
	}
	ret = mr75203_read_channel(pvt, (uintptr_t)context, &milli);

	if (!ret) {
		*value = milli;
	}
	return ret;
}

static int sensor_get_config(void *context, uint32_t *config)
{
	*config = !!(sensor_enabled & BIT((uintptr_t)context));
	return 0;
}

static int sensor_set_config(void *context, uint32_t config)
{
	if (config & 0x1fc) {
		return -EINVAL;
	}
	if (config & ~1U) {
		return -ENOTSUP;
	}
	/* Sampling remains active for the firmware's own monitoring. */
	if (config) {
		sensor_enabled |= BIT((uintptr_t)context);
	} else {
		sensor_enabled &= ~BIT((uintptr_t)context);
	}
	return 0;
}

static int sensor_lookup(void *context, uint32_t id, struct scmi_sensor *sensor, char name[16])
{
	unsigned int temperatures = mr75203_temperature_count(pvt);
	bool temperature = id < temperatures;

	ARG_UNUSED(context);
	numbered_name(name, temperature ? "temp" : "voltage", temperature ? id : id - temperatures);
	*sensor = (struct scmi_sensor){
		.name = name,
		.context = (void *)(uintptr_t)id,
		.type = temperature ? 2 : 5,
		.scale = -3,
		.read = sensor_read,
		.get_config = sensor_get_config,
		.set_config = sensor_set_config,
	};
	return 0;
}

/* Match the vendor AON over-temperature alarm: power off, never throttle. */
void scmi_resources_poll(void)
{
	int32_t milli, hottest = INT32_MIN;
	uint32_t now;

	counter_get_value(timebase, &now);
	if (now - thermal_sampled < counter_get_frequency(timebase) / 10) {
		return;
	}
	thermal_sampled = now;
	for (int i = 0; i < mr75203_temperature_count(pvt); i++) {
		if (!mr75203_read_channel(pvt, i, &milli)) {
			hottest = MAX(hottest, milli);
		}
	}
	/* Two consecutive samples reject a single corrupted conversion. */
	thermal_hits = hottest >= DT_PROP(USER, zhihe_critical_millicelsius)
			       ? MIN(thermal_hits + 1, 2)
			       : 0;
	if (thermal_hits >= 2) {
		(void)a210_system_request(system_controller, 0);
	}
}

static int system_request(void *context, uint32_t state)
{
	return a210_system_request(context, state);
}

static int power_set(void *context, bool on)
{
	return a210_power_set_on(context, on);
}

static int power_get(void *context, bool *on)
{
	return a210_power_get_on(context, on);
}

#define POWER_DOMAIN(node, prop, i)                                                                \
	{.name = DT_PROP(DT_PHANDLE_BY_IDX(node, prop, i), domain_name),                           \
	 .context = (void *)DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node, prop, i)),                       \
	 .set_on = power_set,                                                                      \
	 .get_on = power_get},
static const struct scmi_power_domain power_domains[] = {
	DT_FOREACH_PROP_ELEM(USER, power_domains, POWER_DOMAIN)};

int scmi_resources_init(struct scmi_server *server)
{
	if (!device_is_ready(clocks) || !device_is_ready(resets) || !device_is_ready(pvt)) {
		return -ENODEV;
	}
	for (size_t i = 0; i < ARRAY_SIZE(power_domains); i++) {
		if (!device_is_ready(power_domains[i].context)) {
			return -ENODEV;
		}
	}
	server->power_domains = power_domains;
	server->power_count = ARRAY_SIZE(power_domains);
	server->clock_count = DT_PROP_LEN(USER, clocks);
	server->clock_lookup = clock_lookup;
	server->reset_count = DT_PROP_LEN(USER, resets);
	server->reset_lookup = reset_lookup;
	server->sensor_count = mr75203_channel_count(pvt);
	if (!server->sensor_count || server->sensor_count >= 32) {
		return -ERANGE;
	}
	sensor_enabled = BIT(server->sensor_count) - 1;
	server->sensor_lookup = sensor_lookup;
	if (!device_is_ready(system_controller)) {
		return -ENODEV;
	}
	if (!device_is_ready(timebase)) {
		return -ENODEV;
	}
	counter_get_value(timebase, &thermal_sampled);
	server->system_context = (void *)system_controller;
	server->system_request = system_request;
	return 0;
}
