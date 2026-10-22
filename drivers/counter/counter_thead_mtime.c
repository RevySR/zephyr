/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#define DT_DRV_COMPAT thead_e902_mtime
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/sys_io.h>

struct mtime_config {
	struct counter_config_info info;
	uintptr_t base;
};

static int mtime_start(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0; /* Free running; its clock is owned by the boot firmware. */
}

static int mtime_stop(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -ENOTSUP;
}

static int mtime_get(const struct device *dev, uint32_t *ticks)
{
	const struct mtime_config *config = dev->config;
	*ticks = sys_read32(config->base);
	return 0;
}

static uint32_t mtime_top(const struct device *dev)
{
	ARG_UNUSED(dev);
	return UINT32_MAX;
}

static DEVICE_API(counter, mtime_api) = {
	.start = mtime_start,
	.stop = mtime_stop,
	.get_value = mtime_get,
	.get_top_value = mtime_top,
};

#define MTIME_DEFINE(inst)                                                                         \
	static const struct mtime_config config_##inst = {                                         \
		.info = {.max_top_value = UINT32_MAX,                                              \
			 .freq = DT_INST_PROP(inst, clock_frequency),                              \
			 .flags = COUNTER_CONFIG_INFO_COUNT_UP},                                   \
		.base = DT_INST_REG_ADDR(inst),                                                    \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, &config_##inst, PRE_KERNEL_1,                \
			      CONFIG_COUNTER_INIT_PRIORITY, &mtime_api);

DT_INST_FOREACH_STATUS_OKAY(MTIME_DEFINE)
