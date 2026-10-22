/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include "cpu_dvfs.h"
#include "xuantie_cpu_pll.h"

struct cpu_config {
	uintptr_t pll, mux, backup, gmac, ccu, fabric, video;
	const struct device *timer;
	const struct device *supply;
	const struct device *sram;
	const struct cpu_dvfs_opp *opps;
	size_t count;
	bool th1520;
};
struct cpu_data {
	struct cpu_dvfs dvfs;
	struct xuantie_cpu_clock clock;
	const struct device *dev;
};

static uint32_t read_register(void *context, uintptr_t reg)
{
	ARG_UNUSED(context);
	return sys_read32(reg);
}
static void write_register(void *context, uint32_t value, uintptr_t reg)
{
	ARG_UNUSED(context);
	sys_write32(value, reg);
	__asm__ volatile("fence iorw, iorw" ::: "memory");
}
static uint32_t read_ticks(void *context)
{
	const struct cpu_config *c = context;
	uint32_t ticks = 0;

	counter_get_value(c->timer, &ticks);
	return ticks;
}
static void delay_us(void *context, uint32_t us)
{
	const struct cpu_config *c = context;
	uint32_t start = read_ticks(context);
	uint32_t ticks = DIV_ROUND_UP((uint64_t)counter_get_frequency(c->timer) * us, 1000000);

	while ((uint32_t)(read_ticks(context) - start) < ticks) {
	}
}
static int raw_get(void *context, uint32_t *hz)
{
	struct cpu_data *d = context;

	return xuantie_cpu_clock_get_rate(&d->clock, hz);
}
static int raw_set(void *context, uint32_t hz)
{
	struct cpu_data *d = context;
	int ret = xuantie_cpu_clock_set_rate(&d->clock, hz);

	d->dvfs.faulted |= d->clock.faulted;
	return ret;
}

static int get_voltage(void *context, unsigned int supply, int32_t *uv)
{
	struct cpu_data *d = context;
	const struct cpu_config *c = d->dev->config;

	return regulator_get_voltage(supply ? c->sram : c->supply, uv);
}
static int set_voltage(void *context, unsigned int supply, int32_t uv)
{
	struct cpu_data *d = context;
	const struct cpu_config *c = d->dev->config;

	return regulator_set_voltage(supply ? c->sram : c->supply, uv, uv);
}
static const struct cpu_dvfs_ops dvfs_ops = {
	.get_rate = raw_get,
	.set_rate = raw_set,
	.get_voltage = get_voltage,
	.set_voltage = set_voltage,
};
static int get_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *hz)
{
	return sys ? -EINVAL : raw_get(dev->data, hz);
}
static int set_rate(const struct device *dev, clock_control_subsys_t sys,
		    clock_control_subsys_rate_t rate)
{
	struct cpu_data *d = dev->data;

	return sys ? -EINVAL : cpu_dvfs_set_rate(&d->dvfs, (uintptr_t)rate);
}
static int cpu_init(const struct device *dev)
{
	const struct cpu_config *c = dev->config;
	struct cpu_data *d = dev->data;
	uint32_t hz;
	int32_t uv, sram_uv = 0;
	bool matched = false;
	int ret;

	if (!device_is_ready(c->timer) || !device_is_ready(c->supply) ||
	    !counter_get_frequency(c->timer) || !c->count ||
	    (c->sram && !device_is_ready(c->sram))) {
		return -ENODEV;
	}
	d->dev = dev;
	d->clock = (struct xuantie_cpu_clock){
		.pll = c->pll,
		.mux = c->mux,
		.backup = c->backup,
		.gmac = c->gmac,
		.ccu = c->ccu,
		.fabric = c->fabric,
		.video = c->video,
		.th1520 = c->th1520,
		.timer_hz = counter_get_frequency(c->timer),
		.context = (void *)c,
		.read32 = read_register,
		.write32 = write_register,
		.ticks = read_ticks,
		.delay_us = delay_us,
	};
	d->dvfs = (struct cpu_dvfs){c->opps, c->count, &dvfs_ops, d, false, c->sram ? 2 : 1};
	ret = raw_get(d, &hz);
	if (ret) {
		return ret;
	}
	ret = get_voltage(d, 0, &uv);
	if (ret) {
		return ret;
	}
	if (c->sram) {
		ret = get_voltage(d, 1, &sram_uv);
		if (ret) {
			return ret;
		}
	}
	for (size_t i = 0; i < c->count; i++) {
		if (!c->opps[i].hz || (i && (c->opps[i].hz <= c->opps[i - 1].hz ||
					     c->opps[i].uv < c->opps[i - 1].uv))) {
			return -EINVAL;
		}
		if (!matched && c->opps[i].hz >= hz) {
			if (uv < c->opps[i].uv || sram_uv < c->opps[i].sram_uv) {
				return -ERANGE;
			}
			matched = true;
		}
	}
	if (!matched) {
		return -ERANGE;
	}
	return xuantie_cpu_clock_prepare(&d->clock);
}
static DEVICE_API(clock_control, cpu_api) = {.get_rate = get_rate, .set_rate = set_rate};

#define CHECK_OPP(node)                                                                            \
	BUILD_ASSERT(DT_PROP_LEN(node, opp_hz) == 2 && DT_PROP_BY_IDX(node, opp_hz, 0) == 0);
#define OPP(node)                                                                                  \
	{DT_PROP_BY_IDX(node, opp_hz, 1), DT_PROP_BY_IDX(node, opp_microvolt, 0),                  \
	 COND_CODE_1(DT_PROP_HAS_IDX(node, opp_microvolt, 1), \
		     (DT_PROP_BY_IDX(node, opp_microvolt, 1)), (0))},
#define SRAM_SUPPLY(node)                                                                          \
	COND_CODE_1(DT_NODE_HAS_PROP(node, cpu_sram_supply), \
		    (DEVICE_DT_GET(DT_PHANDLE(node, cpu_sram_supply))), (NULL))
#define DEFINE_CPU(node, is_th1520)                                                                \
	DT_FOREACH_CHILD_STATUS_OKAY(DT_PHANDLE(node, operating_points_v2), CHECK_OPP)             \
	static const struct cpu_dvfs_opp opps_##node[] = {                                         \
		DT_FOREACH_CHILD_STATUS_OKAY(DT_PHANDLE(node, operating_points_v2), OPP)};         \
	static struct cpu_data data_##node;                                                        \
	static const struct cpu_config config_##node = {                                           \
		.pll = DT_REG_ADDR_BY_NAME(node, pll),                                             \
		.mux = DT_REG_ADDR_BY_NAME(node, mux),                                             \
		.backup = DT_REG_ADDR_BY_NAME_OR(node, backup, 0),                                 \
		.gmac = DT_REG_ADDR_BY_NAME_OR(node, gmac, 0),                                     \
		.fabric = DT_REG_ADDR_BY_NAME_OR(node, fabric, 0),                                 \
		.video = DT_REG_ADDR_BY_NAME_OR(node, video, 0),                                   \
		.ccu = DT_REG_ADDR_BY_NAME_OR(node, ccu, 0),                                       \
		.timer = DEVICE_DT_GET(DT_PHANDLE(node, timeout_counter)),                         \
		.sram = SRAM_SUPPLY(node),                                                         \
		.supply = DEVICE_DT_GET(DT_PHANDLE(node, cpu_supply)),                             \
		.opps = opps_##node,                                                               \
		.count = ARRAY_SIZE(opps_##node),                                                  \
		.th1520 = is_th1520,                                                               \
	};                                                                                         \
	DEVICE_DT_DEFINE(node, cpu_init, NULL, &data_##node, &config_##node, POST_KERNEL,          \
			 CONFIG_CPU_DVFS_INIT_PRIORITY, &cpu_api);
#define DEFINE_A210(node)   DEFINE_CPU(node, false)
#define DEFINE_TH1520(node) DEFINE_CPU(node, true)
DT_FOREACH_STATUS_OKAY(zhihe_a210_cpu_clock, DEFINE_A210)
DT_FOREACH_STATUS_OKAY(thead_th1520_cpu_clock, DEFINE_TH1520)
