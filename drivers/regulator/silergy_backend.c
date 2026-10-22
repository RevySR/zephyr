/* SPDX-License-Identifier: Apache-2.0 */
#include "silergy_backend.h"
#include <errno.h>
#include <zephyr/dt-bindings/regulator/sy70209.h>

static const int32_t sy70209_ldo_high[] = {
	750000, 1200000, 1800000, 2500000, 2800000, 2900000, 3100000, 3300000,
};
static const int32_t sy70209_ldo_low[] = {
	600000, 650000, 750000, 800000, 900000, 1100000, 1200000, 1800000,
};
static const int32_t sy70209_ldo4[] = {
	600000, 650000, 750000, 800000, 1800000, 2500000, 3100000, 3300000,
};
static const int32_t sy70209_ldo_io[] = {
	900000, 1200000, 1800000, 2500000, 2800000, 2900000, 3100000, 3300000,
};
static const int32_t sy70209_ldo_core[] = {
	750000, 900000, 1050000, 1100000, 1150000, 1200000, 1500000, 1800000,
};

static const struct {
	const int32_t *table;
	uint8_t voltage_reg, voltage_shift, enable_reg, enable_mask;
} sy70209_ldos[] = {
	[0] = {sy70209_ldo_high, 0x11, 3, 0x3e, 1U << 1},
	[1] = {sy70209_ldo_high, 0x12, 0, 0x3e, 1U << 2},
	[2] = {sy70209_ldo_low, 0x12, 3, 0x3e, 1U << 3},
	[3] = {sy70209_ldo4, 0x13, 0, 0x3e, 1U << 4},
	[4] = {sy70209_ldo_io, 0x13, 3, 0x3e, 1U << 5},
	[6] = {sy70209_ldo_io, 0x14, 1, 0x3e, 1U << 7},
	[7] = {sy70209_ldo_core, 0x14, 4, 0x3f, 1U << 0},
	[8] = {sy70209_ldo_core, 0x15, 0, 0x3f, 1U << 1},
	[9] = {sy70209_ldo_high, 0x15, 3, 0x3f, 1U << 2},
};

int silergy_rail_init(struct silergy_rail *rail, unsigned int output)
{
	static const uint8_t sy70300_voltage[] = {0x48, 0x7c, 0x62};
	static const uint8_t sy70300_enable[] = {0x49, 0x7d, 0x63};

	if (!rail || !rail->bus || !rail->bus->read8 || !rail->bus->write8 ||
	    !rail->bus->delay_us || rail->address > 0x7f || rail->parent_address > 0x7f ||
	    rail->min_uv > rail->max_uv || !rail->ramp_uv_per_us || !rail->enable_delay_us ||
	    rail->enable_delay_us > 20000) {
		return -EINVAL;
	}
	rail->voltage_table = NULL;
	rail->voltage_shift = 0;
	if (rail->type == SILERGY_SY70209) {
		if (output >= SY70209_LDO(1) && output <= SY70209_LDO(10)) {
			unsigned int index = output - SY70209_LDO(1);

			if (!sy70209_ldos[index].table) {
				return -EINVAL;
			}
			/* The module's peripheral LDOs have fixed voltage policies. */
			if (rail->min_uv != rail->max_uv) {
				return -ENOTSUP;
			}
			rail->voltage_table = sy70209_ldos[index].table;
			rail->voltage_reg = sy70209_ldos[index].voltage_reg;
			rail->voltage_shift = sy70209_ldos[index].voltage_shift;
			rail->voltage_mask = 7U << rail->voltage_shift;
			rail->enable_reg = sy70209_ldos[index].enable_reg;
			rail->enable_mask = sy70209_ldos[index].enable_mask;
			rail->selector_step_uv = 1;
			for (unsigned int i = 0; i < 8; i++) {
				if (rail->min_uv == rail->voltage_table[i]) {
					return 0;
				}
			}
			return -ERANGE;
		}
		if (output < 1 || output > 8) {
			return -EINVAL;
		}
		rail->voltage_reg = 8 + output;
		rail->voltage_mask = output == 1 || output == 3 || output == 4 ? 0x3f : 0x7f;
		rail->enable_reg = 0x3d;
		rail->enable_mask = 1U << (output - 1);
		rail->selector_base_uv = output == 1 ? 1200000 :
			output == 2 ? 1712500 : output == 3 ? 712500 :
			output == 4 ? 300000 : 600000;
		rail->selector_step_uv = output == 1 ? 37500 : 12500;
	} else if (rail->type == SILERGY_SY70300) {
		if (output < 1 || output > 3) {
			return -EINVAL;
		}
		rail->voltage_reg = sy70300_voltage[output - 1];
		rail->voltage_mask = 0xff;
		rail->enable_reg = sy70300_enable[output - 1];
		rail->enable_mask = 1;
		rail->selector_base_uv = output == 1 ? 300000 : 450000;
		rail->selector_step_uv = 5000;
	} else {
		return -EINVAL;
	}
	if (rail->min_uv < rail->selector_base_uv ||
	    rail->max_uv > rail->selector_base_uv + rail->voltage_mask * rail->selector_step_uv ||
	    ((int64_t)rail->min_uv - rail->selector_base_uv) % rail->selector_step_uv ||
	    ((int64_t)rail->max_uv - rail->selector_base_uv) % rail->selector_step_uv) {
		return -ERANGE;
	}
	return 0;
}

static int probe(const struct silergy_rail *rail)
{
	uint8_t value;
	int ret;

	if (rail->type != SILERGY_SY70209 && rail->type != SILERGY_SY70300) {
		return -ENODEV;
	}
	ret = rail->bus->read8(rail->bus->context, rail->address,
			       rail->type == SILERGY_SY70209 ? 0x58 : 0x01, &value);
	if (ret) {
		return ret;
	}
	if (value != (rail->type == SILERGY_SY70209 ? 0x88 : 0xff)) {
		return -ENODEV;
	}
	if (rail->type == SILERGY_SY70300) {
		ret = rail->bus->read8(rail->bus->context, rail->address, 0x02, &value);
		if (ret || value > 1) {
			return ret ? ret : -ENODEV;
		}
	}
	return 0;
}

static int get_level(void *context, int32_t *uv)
{
	const struct silergy_rail *rail = context;
	uint8_t value;
	int ret = probe(rail);

	if (!ret) {
		ret = rail->bus->read8(rail->bus->context, rail->address, rail->voltage_reg,
				       &value);
	}
	if (!ret) {
		unsigned int selector = (value & rail->voltage_mask) >> rail->voltage_shift;

		*uv = rail->voltage_table ? rail->voltage_table[selector] :
			rail->selector_base_uv + selector * rail->selector_step_uv;
	}
	return ret;
}

static int get_enabled(void *context, bool *enabled)
{
	const struct silergy_rail *rail = context;
	uint8_t value;
	int ret = probe(rail);

	if (ret) {
		return ret;
	}
	if (rail->parent_mask) {
		ret = rail->bus->read8(rail->bus->context, rail->parent_address, rail->parent_reg,
				       &value);
		if (ret) {
			return ret;
		}
		if (!(value & rail->parent_mask)) {
			*enabled = false;
			return 0;
		}
	}
	ret = rail->bus->read8(rail->bus->context, rail->address, rail->enable_reg, &value);
	if (!ret) {
		*enabled = (value & rail->enable_mask) != 0;
	}
	return ret;
}

static int write_verified(const struct silergy_rail *rail, uint8_t reg, uint8_t mask, uint8_t value,
			  uint32_t settle_us)
{
	uint8_t before, after, expected;
	int ret = rail->bus->read8(rail->bus->context, rail->address, reg, &before);

	if (ret) {
		return ret;
	}
	expected = (uint8_t)((before & ~mask) | (value & mask));
	if (expected == before) {
		return 0;
	}
	/* Verify a live timer before changing the rail. */
	ret = rail->bus->delay_us(rail->bus->context, 1);
	if (!ret) {
		ret = rail->bus->write8(rail->bus->context, rail->address, reg, expected);
	}
	if (!ret) {
		ret = rail->bus->delay_us(rail->bus->context, settle_us);
	}
	if (!ret) {
		ret = rail->bus->read8(rail->bus->context, rail->address, reg, &after);
	}
	/* A failed write/settle/readback is indeterminate, never silently retried. */
	return ret ? ret : (after == expected ? 0 : -EIO);
}

static int set_level(void *context, int32_t uv)
{
	const struct silergy_rail *rail = context;
	int32_t old_uv;
	int64_t selector, difference;
	int ret;
	bool enabled;

	if (uv < rail->min_uv || uv > rail->max_uv || rail->selector_step_uv <= 0 ||
	    !rail->ramp_uv_per_us || !rail->bus->delay_us) {
		return -ERANGE;
	}
	if (rail->voltage_table) {
		for (selector = 0; selector < 8; selector++) {
			if (uv == rail->voltage_table[selector]) {
				break;
			}
		}
		if (selector == 8) {
			return -ERANGE;
		}
	} else {
		difference = (int64_t)uv - rail->selector_base_uv;
		selector = difference / rail->selector_step_uv;
		if (difference < 0 || difference % rail->selector_step_uv ||
		    selector > rail->voltage_mask) {
			return -ERANGE;
		}
	}
	ret = get_enabled(context, &enabled);
	if (ret || !enabled) {
		return ret ? ret : -EACCES;
	}
	ret = get_level(context, &old_uv);
	if (ret) {
		return ret;
	}
	difference = (int64_t)uv - old_uv;
	if (difference < 0) {
		difference = -difference;
	}
	uint64_t delay = rail->enable_delay_us +
			 (uint64_t)(difference + rail->ramp_uv_per_us - 1) / rail->ramp_uv_per_us;

	if (delay > 20000) {
		return -ERANGE;
	}
	return write_verified(rail, rail->voltage_reg, rail->voltage_mask,
			      (uint8_t)(selector << rail->voltage_shift),
			      (uint32_t)delay);
}

static int set_enabled(void *context, bool enabled)
{
	const struct silergy_rail *rail = context;
	bool current;
	int ret;

	if (!enabled && rail->always_on) {
		return -EACCES;
	}
	ret = get_enabled(context, &current);
	if (ret || current == enabled) {
		return ret;
	}
	/* Observe the shared supply; only this output's enable may be changed. */
	if (rail->parent_mask) {
		uint8_t value;

		ret = rail->bus->read8(rail->bus->context, rail->parent_address, rail->parent_reg,
				       &value);
		if (ret || !(value & rail->parent_mask)) {
			return ret ? ret : -EACCES;
		}
	}
	if (!rail->bus->delay_us || rail->enable_delay_us > 20000) {
		return -ENOTSUP;
	}
	return write_verified(rail, rail->enable_reg, rail->enable_mask,
			      enabled ? rail->enable_mask : 0, rail->enable_delay_us);
}

const struct pmic_output_api silergy_voltage_ops = {
	.get_level = get_level,
	.set_level = set_level,
	.get_enabled = get_enabled,
	.set_enabled = set_enabled,
};
