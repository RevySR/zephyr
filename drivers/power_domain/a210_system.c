/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT zhihe_a210_system_controller

#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/power_domain/a210.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#define SY70209_SYS_CONTROL   0x05
#define SY70209_POWER_ON      BIT(7)
#define GLOBAL_RESET_ENABLE   BIT(0)
#define GLOBAL_RESET_REQUEST  BIT(0)
#define TRANSITION_TIMEOUT_MS 100

struct system_config {
	uintptr_t enable, request;
	struct i2c_dt_spec pmic;
	const struct device *timer;
};

int a210_system_request(const struct device *dev, uint32_t state)
{
	const struct system_config *c = dev->config;
	uint32_t start, now;
	int ret;

	if (state == A210_SYSTEM_SHUTDOWN) {
		ret = i2c_reg_update_byte_dt(&c->pmic, SY70209_SYS_CONTROL, SY70209_POWER_ON, 0);
		if (ret) {
			return ret;
		}
	} else if (state == A210_SYSTEM_COLD_RESET) {
		sys_write32(sys_read32(c->enable) | GLOBAL_RESET_ENABLE, c->enable);
		__asm__ volatile("fence iorw, iorw" ::: "memory");
		sys_write32(GLOBAL_RESET_REQUEST, c->request);
		__asm__ volatile("fence iorw, iorw" ::: "memory");
	} else {
		return -ENOTSUP;
	}
	/* A successful full-system transition does not return to the caller. */
	counter_get_value(c->timer, &start);
	do {
		counter_get_value(c->timer, &now);
	} while ((uint32_t)(now - start) <
		 counter_get_frequency(c->timer) / (MSEC_PER_SEC / TRANSITION_TIMEOUT_MS));
	return -EIO;
}

static int init(const struct device *dev)
{
	const struct system_config *c = dev->config;

	return i2c_is_ready_dt(&c->pmic) && device_is_ready(c->timer) ? 0 : -ENODEV;
}

static const struct system_config config = {
	.enable = DT_INST_REG_ADDR_BY_NAME(0, enable),
	.request = DT_INST_REG_ADDR_BY_NAME(0, request),
	.pmic = I2C_DT_SPEC_GET(DT_INST_PHANDLE(0, pmic)),
	.timer = DEVICE_DT_GET(DT_INST_PHANDLE(0, timeout_counter)),
};
DEVICE_DT_INST_DEFINE(0, init, NULL, NULL, &config, POST_KERNEL,
		      CONFIG_A210_SYSTEM_CONTROL_INIT_PRIORITY, NULL);
