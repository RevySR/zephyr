/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT snps_designware_i2c_poll
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/sys_io.h>
#include "i2c_dw_poll_core.h"

struct poll_config {
	uintptr_t base;
	const struct device *counter;
};
struct poll_data {
	struct dw_i2c_poll core;
	bool adopted;
	int error;
};

static uint32_t poll_read(void *context, uint32_t offset)
{
	const struct device *dev = context;
	const struct poll_config *config = dev->config;

	return sys_read32(config->base + offset);
}

static void poll_write(void *context, uint32_t offset, uint32_t value)
{
	const struct device *dev = context;
	const struct poll_config *config = dev->config;

	sys_write32(value, config->base + offset);
	/* Order device I/O as well as memory; a data-only fence is insufficient. */
	__asm__ volatile("fence iorw, iorw" ::: "memory");
}

static uint32_t poll_ticks(void *context)
{
	const struct device *dev = context;
	const struct poll_config *config = dev->config;
	uint32_t ticks = 0;
	(void)counter_get_value(config->counter, &ticks);
	return ticks;
}

static int poll_configure(const struct device *dev, uint32_t config)
{
	uint32_t speed = I2C_SPEED_GET(config);
	uint32_t actual = (poll_read((void *)dev, 0) >> 1) & 3U;

	if (!(config & I2C_MODE_CONTROLLER) || (config & I2C_ADDR_10_BITS)) {
		return -ENOTSUP;
	}
	return ((speed == I2C_SPEED_STANDARD && actual == 1) ||
		(speed == I2C_SPEED_FAST && actual == 2))
		       ? 0
		       : -ENOTSUP;
}

static int poll_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t count,
			 uint16_t addr)
{
	struct poll_data *data = dev->data;
	int ret;

	if (!msgs || !count || count > 2 || addr > 0x7f) {
		return -EINVAL;
	}
	for (unsigned int i = 0; i < count; i++) {
		if (!msgs[i].buf || !msgs[i].len || msgs[i].len > 8 ||
		    (msgs[i].flags & ~(I2C_MSG_READ | I2C_MSG_STOP | I2C_MSG_RESTART))) {
			return -ENOTSUP;
		}
	}
	if (!(msgs[count - 1].flags & I2C_MSG_STOP)) {
		return -ENOTSUP;
	}
	if (count == 2 && !(msgs[0].flags & I2C_MSG_STOP) &&
	    ((msgs[0].flags & I2C_MSG_READ) || !(msgs[1].flags & I2C_MSG_READ))) {
		return -ENOTSUP;
	}
	if (!data->adopted) {
		data->adopted = true;
		data->error = dw_i2c_poll_adopt(&data->core);
	}
	if (data->error) {
		return data->error;
	}
	if (count == 2 && !(msgs[0].flags & I2C_MSG_STOP)) {
		return dw_i2c_poll_transfer(&data->core, (uint8_t)addr, msgs[0].buf, msgs[0].len,
					    msgs[1].buf, msgs[1].len);
	}
	for (unsigned int i = 0; i < count; i++) {
		bool read = msgs[i].flags & I2C_MSG_READ;

		ret = dw_i2c_poll_transfer(&data->core, (uint8_t)addr, read ? NULL : msgs[i].buf,
					   read ? 0 : msgs[i].len, read ? msgs[i].buf : NULL,
					   read ? msgs[i].len : 0);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int poll_init(const struct device *dev)
{
	const struct poll_config *config = dev->config;
	struct poll_data *data = dev->data;
	uint32_t hz;

	if (!device_is_ready(config->counter)) {
		return -ENODEV;
	}
	hz = counter_get_frequency(config->counter);
	if (!hz || hz % 1000000) {
		return -EINVAL;
	}
	data->core = (struct dw_i2c_poll){.context = (void *)dev,
					  .read32 = poll_read,
					  .write32 = poll_write,
					  .ticks = poll_ticks,
					  .ticks_per_us = hz / 1000000,
					  .poll_limit = 1000000};
	return 0;
}

static DEVICE_API(i2c, poll_api) = {.configure = poll_configure, .transfer = poll_transfer};
#define POLL_DEFINE(inst)                                                                          \
	static struct poll_data data_##inst;                                                       \
	static const struct poll_config config_##inst = {                                          \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.counter = DEVICE_DT_GET(DT_INST_PHANDLE(inst, timeout_counter)),                  \
	};                                                                                         \
	I2C_DEVICE_DT_INST_DEFINE(inst, poll_init, NULL, &data_##inst, &config_##inst,             \
				  POST_KERNEL, CONFIG_I2C_INIT_PRIORITY, &poll_api);
DT_INST_FOREACH_STATUS_OKAY(POLL_DEFINE)
