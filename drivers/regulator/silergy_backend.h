/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_REGULATOR_SILERGY_BACKEND_H_
#define ZEPHYR_DRIVERS_REGULATOR_SILERGY_BACKEND_H_
#include "pmic_output.h"

struct silergy_bus {
	void *context;
	int (*read8)(void *context, uint8_t address, uint8_t reg, uint8_t *value);
	int (*write8)(void *context, uint8_t address, uint8_t reg, uint8_t value);
	int (*delay_us)(void *context, uint32_t us);
};

enum silergy_type {
	SILERGY_SY70209,
	SILERGY_SY70300
};
struct silergy_rail {
	const struct silergy_bus *bus;
	enum silergy_type type;
	uint8_t address, voltage_reg, voltage_mask, enable_reg, enable_mask;
	int32_t selector_base_uv, selector_step_uv;
	int32_t min_uv, max_uv;
	uint32_t ramp_uv_per_us, enable_delay_us;
	bool always_on;
	/* Optional shared external enable; the rail driver never changes it. */
	uint8_t parent_address, parent_reg, parent_mask;
	/* SY70209 LDO selectors share bytes and use non-linear voltage tables. */
	const int32_t *voltage_table;
	uint8_t voltage_shift;
};

/* Resolve and validate one output without touching the bus. */
int silergy_rail_init(struct silergy_rail *rail, unsigned int output);

extern const struct pmic_output_api silergy_voltage_ops;
#endif
