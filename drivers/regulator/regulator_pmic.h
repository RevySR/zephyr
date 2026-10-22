/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_REGULATOR_PMIC_H_
#define ZEPHYR_DRIVERS_REGULATOR_PMIC_H_
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include "pmic_output.h"

/* Common prefix of each provider's configuration and runtime state. */
struct pmic_regulator_config {
	struct regulator_common_config common;
	struct i2c_dt_spec bus;
	const struct device *counter;
	uint32_t settle_us;
	int (*init_output)(const struct device *dev);
};
struct pmic_regulator_data {
	struct regulator_common_data common;
	struct pmic_output output;
};
int pmic_bus_read(void *context, uint8_t address, uint16_t reg, unsigned int width, uint8_t *value,
		  size_t size, bool restart);
int pmic_bus_write(void *context, uint8_t address, uint16_t reg, unsigned int width,
		   const uint8_t *value, size_t size);
int pmic_bus_delay(void *context, uint32_t us);
int pmic_regulator_init(const struct device *dev);
extern const struct regulator_driver_api pmic_regulator_api;

#define PMIC_PARENT(node) DT_GPARENT(node)
#define PMIC_REGULATOR_CONFIG(node, init_fn)                                                       \
	{                                                                                          \
		.common = REGULATOR_DT_COMMON_CONFIG_INIT(node),                                   \
		.bus = I2C_DT_SPEC_GET(PMIC_PARENT(node)),                                         \
		.counter = DEVICE_DT_GET(DT_PHANDLE(PMIC_PARENT(node), timeout_counter)),          \
		.settle_us = DT_PROP(node, settling_time_us),                                      \
		.init_output = init_fn,                                                            \
	}
#define PMIC_REGULATOR_DEVICE(node)                                                                \
	DEVICE_DT_DEFINE(node, pmic_regulator_init, NULL, &data_##node, &config_##node,            \
			 POST_KERNEL, CONFIG_REGULATOR_PMIC_INIT_PRIORITY, &pmic_regulator_api);
#endif
