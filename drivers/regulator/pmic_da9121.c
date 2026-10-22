/* SPDX-License-Identifier: Apache-2.0 */
#include "pmic_backend.h"
#include <errno.h>

static int setup(struct pmic_rail *r)
{
	unsigned int output = r->output;

	if (output) {
		return -EINVAL;
	}
	r->voltage_reg = 0x25;
	r->enable_reg = 0x20;
	r->base_uv = 0;
	r->step_uv = 10000;
	r->selector_min = 30;
	r->selector_max = 190;
	return 0;
}

static int validate(struct pmic_rail *r)
{
	uint8_t value;
	int ret;

	ret = pmic_read_reg(r, 0x48, &value);
	if (ret || value != 5) {
		return ret ? ret : -ENODEV;
	}
	ret = pmic_read_reg(r, 0x49, &value);
	if (ret || (value & 0xf) != 1 || (value >> 4) < 2) {
		return ret ? ret : -ENODEV;
	}
	ret = pmic_read_reg(r, 0x24, &value);
	return ret ? ret : ((value & 0x10) ? -ENOTSUP : 0);
}

const struct pmic_driver pmic_da9121_driver = {
	.setup = setup,
	.validate = validate,
	.register_width = 1,
	.banked_address = false,
};
