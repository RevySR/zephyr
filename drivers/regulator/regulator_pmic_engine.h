/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_REGULATOR_PMIC_ENGINE_H_
#define ZEPHYR_DRIVERS_REGULATOR_PMIC_ENGINE_H_
#include "regulator_pmic.h"
#include "pmic_backend.h"

struct pmic_engine_config {
	struct pmic_regulator_config common;
	const struct pmic_driver *driver;
	unsigned int output;
	uint8_t upper_address;
	bool high_range;
};
struct pmic_engine_data {
	struct pmic_regulator_data common;
	struct pmic_backend_io io;
	struct pmic_backend chip;
	struct pmic_rail rail;
};
int pmic_engine_init(const struct device *dev);
#define PMIC_DEFINE(node, chip_driver)                                                             \
	static struct pmic_engine_data data_##node;                                                \
	static const struct pmic_engine_config config_##node = {                                   \
		.common = PMIC_REGULATOR_CONFIG(node, pmic_engine_init),                           \
		.driver = chip_driver,                                                             \
		.output = DT_REG_ADDR(node),                                                       \
		.upper_address = DT_PROP_OR(PMIC_PARENT(node), upper_bank_address, 0),             \
		.high_range = DT_PROP_OR(node, slg_high_range, 0),                                 \
	};                                                                                         \
	PMIC_REGULATOR_DEVICE(node)
#endif
