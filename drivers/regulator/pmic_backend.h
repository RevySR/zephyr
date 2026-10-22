/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_REGULATOR_PMIC_BACKEND_H_
#define ZEPHYR_DRIVERS_REGULATOR_PMIC_BACKEND_H_
#include "pmic_output.h"

struct pmic_rail;

/* Immutable chip operations, selected by the regulator compatible. */
struct pmic_driver {
	int (*setup)(struct pmic_rail *rail);
	int (*validate)(struct pmic_rail *rail);
	unsigned int register_width;
	bool banked_address;
};

struct pmic_backend_io {
	void *context;
	int (*read)(void *context, uint8_t address, uint16_t reg, unsigned int width, uint8_t *data,
		    size_t size, bool restart);
	int (*write)(void *context, uint8_t address, uint16_t reg, unsigned int width,
		     const uint8_t *data, size_t size);
	int (*delay_us)(void *context, uint32_t us);
};
struct pmic_backend {
	const struct pmic_backend_io *io;
	const struct pmic_driver *driver;
	uint8_t address, upper_address;
};
struct pmic_rail {
	struct pmic_backend *pmic;
	unsigned int output;
	uint16_t voltage_reg, enable_reg;
	uint8_t mask, shift, enable_mask, selector_min, selector_max;
	int32_t base_uv, step_uv, min_uv, max_uv;
	uint32_t settle_us;
	bool always_on, slg_high_range;
};

/* DA9063: 0..5 bucks, 6..16 LDO1..11, 17 merged cores, 18 merged MEM/IO.
 * DA9121: 0 buck. SLG51000: 0..6 LDO1..7.
 */
int pmic_rail_init(struct pmic_rail *rail, struct pmic_backend *pmic, unsigned int output,
		   int32_t min_uv, int32_t max_uv, uint32_t settle_us, bool always_on,
		   bool slg_high_range);
int pmic_read_reg(struct pmic_rail *rail, uint16_t reg, uint8_t *value);
extern const struct pmic_output_api pmic_backend_voltage_ops;
extern const struct pmic_driver pmic_da9063_driver;
extern const struct pmic_driver pmic_da9121_driver;
extern const struct pmic_driver pmic_slg51000_driver;

#endif
