/* SPDX-License-Identifier: Apache-2.0 */
#include "pmic_backend.h"
#include <errno.h>
#include <string.h>

int pmic_read_reg(struct pmic_rail *r, uint16_t reg, uint8_t *value)
{
	struct pmic_backend *p = r->pmic;
	uint8_t address = p->address;
	unsigned int width = p->driver->register_width;

	if (p->driver->banked_address && reg >= 0x100) {
		address = p->upper_address;
		reg -= 0x100;
	}
	return p->io->read(p->io->context, address, reg, width, value, 1, false);
}

static int write_reg(struct pmic_rail *r, uint16_t reg, uint8_t value)
{
	struct pmic_backend *p = r->pmic;

	return p->io->write(p->io->context, p->address, reg, p->driver->register_width, &value, 1);
}

static int validate_mode(struct pmic_rail *r)
{
	return r->pmic->driver->validate ? r->pmic->driver->validate(r) : 0;
}

static int get_level(void *context, int32_t *uv)
{
	struct pmic_rail *r = context;
	uint8_t value;
	int ret = validate_mode(r);

	if (ret) {
		return ret;
	}
	ret = pmic_read_reg(r, r->voltage_reg, &value);
	if (!ret) {
		unsigned int selector = (value & r->mask) >> r->shift;

		if (selector < r->selector_min || selector > r->selector_max) {
			return -ERANGE;
		}
		*uv = r->base_uv + (int32_t)selector * r->step_uv;
	}
	return ret;
}

static int get_enabled(void *context, bool *enabled)
{
	struct pmic_rail *r = context;
	uint8_t value;
	int ret;

	ret = validate_mode(r);
	if (!ret) {
		ret = pmic_read_reg(r, r->enable_reg, &value);
	}
	if (!ret) {
		*enabled = !!(value & r->enable_mask);
	}
	return ret;
}

static int update(struct pmic_rail *r, uint16_t reg, uint8_t mask, uint8_t value)
{
	uint8_t before, after;
	int ret = pmic_read_reg(r, reg, &before);

	if (ret) {
		return ret;
	}
	value = (uint8_t)((before & ~mask) | (value & mask));
	if (value == before) {
		return 0;
	}
	ret = r->pmic->io->delay_us(r->pmic->io->context, 1);
	if (!ret) {
		ret = write_reg(r, reg, value);
	}
	if (!ret) {
		ret = r->pmic->io->delay_us(r->pmic->io->context, r->settle_us);
	}
	if (!ret) {
		ret = pmic_read_reg(r, reg, &after);
	}
	return ret ? ret : (after == value ? 0 : -EIO);
}

static int set_level(void *context, int32_t uv)
{
	struct pmic_rail *r = context;
	int64_t diff = (int64_t)uv - r->base_uv;
	bool enabled;
	int ret;

	if (uv < r->min_uv || uv > r->max_uv || diff < 0 || diff % r->step_uv) {
		return -ERANGE;
	}
	ret = get_enabled(r, &enabled);
	if (ret || !enabled) {
		return ret ? ret : -EACCES;
	}
	return update(r, r->voltage_reg, r->mask, (uint8_t)((diff / r->step_uv) << r->shift));
}

static int set_enabled(void *context, bool enabled)
{
	struct pmic_rail *r = context;
	int ret;

	if (r->always_on && !enabled) {
		return -EACCES;
	}
	ret = validate_mode(r);
	return ret ? ret : update(r, r->enable_reg, r->enable_mask, enabled ? r->enable_mask : 0);
}

int pmic_rail_init(struct pmic_rail *r, struct pmic_backend *p, unsigned int output, int32_t min_uv,
		   int32_t max_uv, uint32_t settle_us, bool always_on, bool slg_high_range)
{

	int ret;

	memset(r, 0, sizeof(*r));
	if (!p || !p->driver || !p->driver->setup || !p->io || !p->io->read || !p->io->write ||
	    !p->io->delay_us || p->address > 0x7f || min_uv > max_uv || !settle_us ||
	    settle_us > 20000) {
		return -EINVAL;
	}
	r->pmic = p;
	r->output = output;
	r->min_uv = min_uv;
	r->max_uv = max_uv;
	r->settle_us = settle_us;
	r->always_on = always_on;
	r->slg_high_range = slg_high_range;
	r->mask = 0xff;
	r->enable_mask = 1;
	r->selector_max = 255;
	ret = p->driver->setup(r);
	if (ret) {
		return ret;
	}

	if (min_uv < r->base_uv + r->selector_min * r->step_uv ||
	    max_uv > r->base_uv + r->selector_max * r->step_uv ||
	    ((int64_t)min_uv - r->base_uv) % r->step_uv ||
	    ((int64_t)max_uv - r->base_uv) % r->step_uv) {
		return -ERANGE;
	}
	return 0;
}

const struct pmic_output_api pmic_backend_voltage_ops = {
	.get_level = get_level,
	.set_level = set_level,
	.get_enabled = get_enabled,
	.set_enabled = set_enabled,
};
