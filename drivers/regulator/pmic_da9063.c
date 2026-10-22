/* SPDX-License-Identifier: Apache-2.0 */
#include "pmic_backend.h"
#include <errno.h>

static int setup(struct pmic_rail *r)
{
	unsigned int output = r->output;
	struct pmic_backend *p = r->pmic;

	static const uint8_t vregs[] = {0xa4, 0xa3, 0xa5, 0xa6, 0xa7, 0xa8};
	static const uint8_t eregs[] = {0x21, 0x20, 0x22, 0x23, 0x24, 0x25};
	unsigned int index = output == 17 ? 0 : (output == 18 ? 3 : output);

	if (output > 18 || p->upper_address > 0x7f || p->upper_address == p->address) {
		return -EINVAL;
	}
	if (index < 6) {
		r->voltage_reg = vregs[index];
		r->enable_reg = eregs[index];
		r->base_uv = index < 2 ? 300000 : (index == 2 ? 530000 : 800000);
		r->step_uv = index < 3 ? 10000 : 20000;
		r->mask = 0x7f;
		r->selector_max = 127;
	} else {
		index -= 6;
		r->voltage_reg = (uint16_t)(0xa9 + index);
		r->enable_reg = (uint16_t)(0x26 + index);
		r->base_uv = index == 0 || index == 1 || index == 3 ? 600000 : 900000;
		r->step_uv = index < 4 ? 20000 : 50000;
		r->mask = index == 2 ? 0x7f : 0x3f;
		r->selector_max = r->mask;
		if (index >= 4) {
			r->selector_min = 2;
			r->base_uv -= 100000;
			r->selector_max = 56;
		}
	}
	return 0;
}

static int validate(struct pmic_rail *r)
{
	uint8_t value;
	int ret;

	static const uint8_t bank_reg[] = {0x32, 0x32, 0x32, 0x32, 0x33,
					   0x32, 0x32, 0x32, 0x32, 0x33};
	static const uint8_t bank_mask[] = {1, 2, 4, 8, 1, 0x10, 0x20, 0x40, 0x80, 0x80};
	unsigned int index = r->output == 17 ? 0 : (r->output == 18 ? 3 : r->output);
	uint8_t control_mask = index < 6 ? 0x6e : (index < 10 ? 0xe6 : 0xf6);

	ret = pmic_read_reg(r, 0x181, &value);
	if (ret || value != 0x61) {
		return ret ? ret : -ENODEV;
	}
	/* Never change page, merge configuration or active/suspend bank. */
	ret = pmic_read_reg(r, 0, &value);
	if (ret || (value & 7)) {
		return ret ? ret : -ENOTSUP;
	}
	ret = pmic_read_reg(r, 0x10d, &value);
	if (ret) {
		return ret;
	}
	if (((r->output <= 1 || r->output == 17) && (!!(value & 8) != (r->output == 17))) ||
	    ((r->output == 3 || r->output == 4 || r->output == 18) &&
	     (!!(value & 0x80) != (r->output == 18)))) {
		return -ENOTSUP;
	}
	ret = pmic_read_reg(r, r->enable_reg, &value);
	if (ret || (value & control_mask)) {
		return ret ? ret : -ENOTSUP;
	}
	if (index >= 10) {
		return 0;
	} /* LDO5..11 bank select is in CONT. */
	ret = pmic_read_reg(r, bank_reg[index], &value);
	return ret ? ret : ((value & bank_mask[index]) ? -ENOTSUP : 0);
}

const struct pmic_driver pmic_da9063_driver = {
	.setup = setup,
	.validate = validate,
	.register_width = 1,
	.banked_address = true,
};
