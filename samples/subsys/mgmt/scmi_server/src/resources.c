/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/mgmt/scmi/clock.h>
#include <zephyr/mgmt/scmi/reset.h>
#include "resources.h"

#define USER DT_PATH(zephyr_user)

#if defined(CONFIG_SCMI_SERVER_CLOCK) && DT_NODE_HAS_PROP(USER, clocks)
struct clock_resource {
	const struct device *dev;
	uintptr_t id;
};

static int get_rate(void *context, uint64_t *rate)
{
	const struct clock_resource *r = context;
	uint32_t hz;
	int ret = clock_control_get_rate(r->dev, (void *)r->id, &hz);

	if (!ret) {
		*rate = hz;
	}
	return ret;
}

static int describe_rate(void *context, unsigned int index, uint64_t *rate)
{
	return index ? -EINVAL : get_rate(context, rate);
}

static int get_enabled(void *context, bool *enabled)
{
	const struct clock_resource *r = context;
	enum clock_control_status state = clock_control_get_status(r->dev, (void *)r->id);

	if (state != CLOCK_CONTROL_STATUS_ON && state != CLOCK_CONTROL_STATUS_OFF) {
		return -EIO;
	}
	*enabled = state == CLOCK_CONTROL_STATUS_ON;
	return 0;
}

static int set_enabled(void *context, bool enabled)
{
	const struct clock_resource *r = context;

	return enabled ? clock_control_on(r->dev, (void *)r->id) :
			 clock_control_off(r->dev, (void *)r->id);
}

static const struct scmi_clock_ops clock_ops = {
	.get_rate = get_rate,
	.describe_rate = describe_rate,
	.get_enabled = get_enabled,
	.set_enabled = set_enabled,
};

#define CLOCK_RESOURCE(node, prop, i) \
	{DEVICE_DT_GET(DT_CLOCKS_CTLR_BY_IDX(node, i)), DT_CLOCKS_CELL_BY_IDX(node, i, id)},
static const struct clock_resource clock_resources[] = {
	DT_FOREACH_PROP_ELEM(USER, clocks, CLOCK_RESOURCE)};

#define CLOCK_DOMAIN(node, prop, i) \
	{.name = DT_PROP_BY_IDX(node, clock_names, i), .context = (void *)&clock_resources[i], \
	 .ops = &clock_ops, .rate_count = 1},
static const struct scmi_clock clocks[] = {DT_FOREACH_PROP_ELEM(USER, clocks, CLOCK_DOMAIN)};
#endif

#if defined(CONFIG_SCMI_SERVER_RESET) && DT_NODE_HAS_PROP(USER, resets)
static int set_asserted(void *context, bool asserted)
{
	const struct reset_dt_spec *r = context;

	return asserted ? reset_line_assert_dt(r) : reset_line_deassert_dt(r);
}

#define RESET_RESOURCE(node, prop, i) RESET_DT_SPEC_GET_BY_IDX(node, i),
static const struct reset_dt_spec reset_resources[] = {
	DT_FOREACH_PROP_ELEM(USER, resets, RESET_RESOURCE)};

#define RESET_DOMAIN(node, prop, i) \
	{.name = DT_PROP_BY_IDX(node, reset_names, i), .context = (void *)&reset_resources[i], \
	 .latency_us = UINT32_MAX, .set_asserted = set_asserted},
static const struct scmi_reset_domain resets[] = {
	DT_FOREACH_PROP_ELEM(USER, resets, RESET_DOMAIN)};
#endif

int scmi_resources_init(struct scmi_server *server)
{
	ARG_UNUSED(server);
#if defined(CONFIG_SCMI_SERVER_CLOCK) && DT_NODE_HAS_PROP(USER, clocks)
	for (size_t i = 0; i < ARRAY_SIZE(clock_resources); i++) {
		if (!device_is_ready(clock_resources[i].dev)) {
			return -ENODEV;
		}
	}
	server->clocks = clocks;
	server->clock_count = ARRAY_SIZE(clocks);
#endif
#if defined(CONFIG_SCMI_SERVER_RESET) && DT_NODE_HAS_PROP(USER, resets)
	for (size_t i = 0; i < ARRAY_SIZE(reset_resources); i++) {
		if (!device_is_ready(reset_resources[i].dev)) {
			return -ENODEV;
		}
	}
	server->reset_domains = resets;
	server->reset_count = ARRAY_SIZE(resets);
#endif
	return 0;
}
