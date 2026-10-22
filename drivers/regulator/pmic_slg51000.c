/* SPDX-License-Identifier: Apache-2.0 */
#include "pmic_backend.h"
#include <errno.h>

static const uint16_t slg_regs[] = {0x2000, 0x2200, 0x2300, 0x2500, 0x2700, 0x2900, 0x3100};

static int setup(struct pmic_rail *r)
{
	unsigned int output = r->output;

	if (output >= 7) {
		return -EINVAL;
	}
	r->voltage_reg = slg_regs[output];
	r->enable_reg = 0x110d;
	r->enable_mask = (uint8_t)(1U << output);
	r->base_uv = output < 2 ? (r->slg_high_range ? 2400000 : 1200000)
				: (output == 4 || output == 5 ? 400000 : 1200000);
	r->step_uv = output < 2 || output == 4 || output == 5 ? 5000 : 10000;
	if (r->step_uv == 10000) {
		r->selector_max = 240;
	}
	return 0;
}

static int validate(struct pmic_rail *r)
{
	uint8_t value, min_sel, max_sel;
	int ret;

	ret = pmic_read_reg(r, (uint16_t)(r->voltage_reg + 0x60), &min_sel);
	if (!ret) {
		ret = pmic_read_reg(r, (uint16_t)(r->voltage_reg + 0x61), &max_sel);
	}
	if (ret) {
		return ret;
	}
	if (min_sel > max_sel || r->min_uv < r->base_uv + min_sel * r->step_uv ||
	    r->max_uv > r->base_uv + max_sel * r->step_uv) {
		return -ERANGE;
	}
	if (r->output < 2) {
		ret = pmic_read_reg(r, (uint16_t)(r->voltage_reg + 0x64), &value);
		if (ret || !!(value & 1) != r->slg_high_range) {
			return ret ? ret : -ENOTSUP;
		}
	} else if (r->output == 4 || r->output == 5) {
		ret = pmic_read_reg(r, (uint16_t)(r->voltage_reg + 0x63), &value);
		if (ret || (value & 1)) {
			return ret ? ret : -ENOTSUP; /* bypass is not voltage regulation */
		}
	}
	return 0;
}

const struct pmic_driver pmic_slg51000_driver = {
	.setup = setup,
	.validate = validate,
	.register_width = 2,
	.banked_address = false,
};
