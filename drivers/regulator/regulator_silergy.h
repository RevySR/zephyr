/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_REGULATOR_SILERGY_H_
#define ZEPHYR_DRIVERS_REGULATOR_SILERGY_H_
#include "regulator_pmic.h"
#include "silergy_backend.h"
#include <zephyr/dt-bindings/gpio/gpio.h>

struct silergy_config {
	struct pmic_regulator_config common;
	enum silergy_type type;
	unsigned int output;
	uint32_t ramp_uv_per_us;
	uint8_t parent_address, parent_mask;
};
struct silergy_data {
	struct pmic_regulator_data common;
	struct silergy_bus bus;
	struct silergy_rail rail;
};
int silergy_regulator_init(const struct device *dev);

#define SILERGY_PARENT(node) DT_PARENT(DT_GPIO_CTLR(node, enable_gpios))
#define SILERGY_PARENT_CHECK(node)                                                                 \
	COND_CODE_1(DT_NODE_HAS_PROP(node, enable_gpios), ( \
		BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_GPIO_CTLR(node, enable_gpios), \
			silergy_sy70209_gpio), "Only SY70209 GPIO enables are supported"); \
		BUILD_ASSERT(DT_SAME_NODE(DT_BUS(SILERGY_PARENT(node)), \
			DT_BUS(PMIC_PARENT(node))), "Parent enable must share the I2C bus"); \
		BUILD_ASSERT(DT_GPIO_FLAGS(node, enable_gpios) == GPIO_ACTIVE_HIGH, \
			"Parent enable must be active high"); \
		BUILD_ASSERT(DT_GPIO_PIN(node, enable_gpios) < 8, "Invalid SY70209 GPIO"); \
	), ())
#define SILERGY_PARENT_ADDRESS(node)                                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(node, enable_gpios), \
		(DT_REG_ADDR(SILERGY_PARENT(node))), (0))
#define SILERGY_PARENT_MASK(node)                                                                  \
	COND_CODE_1(DT_NODE_HAS_PROP(node, enable_gpios), \
		(BIT(DT_GPIO_PIN(node, enable_gpios))), (0))
#define SILERGY_DEFINE(node, chip_type)                                                            \
	SILERGY_PARENT_CHECK(node)                                                                 \
	static struct silergy_data data_##node;                                                    \
	static const struct silergy_config config_##node = {                                       \
		.common = PMIC_REGULATOR_CONFIG(node, silergy_regulator_init),                     \
		.type = chip_type,                                                                 \
		.output = DT_REG_ADDR(node),                                                       \
		.ramp_uv_per_us = DT_PROP_OR(node, regulator_ramp_delay, 0),                       \
		.parent_address = SILERGY_PARENT_ADDRESS(node),                                    \
		.parent_mask = SILERGY_PARENT_MASK(node),                                          \
	};                                                                                         \
	PMIC_REGULATOR_DEVICE(node)
#endif
