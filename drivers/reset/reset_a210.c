/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT zhihe_a210_reset

#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/reset/a210.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define RESET_BANKS DT_NUM_REGS(DT_DRV_INST(0))
#define RESET_WORDS 16

struct a210_reset_line {
	uint8_t bank;
	uint8_t offset;
	uint8_t bit;
};

#include "a210_reset_data.h"

struct reset_config {
	uintptr_t base[RESET_BANKS];
	uintptr_t gpu_status;
	uintptr_t vo_status;
};

struct reset_data {
	uint32_t values[RESET_BANKS][RESET_WORDS];
	uint32_t masks[RESET_BANKS][RESET_WORDS];
};

static unsigned int bank_domain(unsigned int bank)
{
	return bank == 0 ? 1 : (bank == 8 ? 2 : 0);
}

static bool accessible(const struct reset_config *c, unsigned int bank)
{
	unsigned int domain = bank_domain(bank);
	uintptr_t status = domain == 1 ? c->gpu_status : c->vo_status;

	return !domain || (sys_read32(status) & 0x1f);
}

static uint32_t read_register(const struct device *dev, unsigned int bank, unsigned int offset)
{
	const struct reset_config *c = dev->config;
	struct reset_data *d = dev->data;

	return accessible(c, bank) ? sys_read32(c->base[bank] + offset)
				   : d->values[bank][offset / 4];
}

static int update_register(const struct device *dev, unsigned int bank, unsigned int offset,
			   uint32_t mask, bool asserted)
{
	const struct reset_config *c = dev->config;
	struct reset_data *d = dev->data;
	uint32_t value = read_register(dev, bank, offset);

	value = asserted ? value & ~mask : value | mask;
	if (accessible(c, bank)) {
		sys_write32(value, c->base[bank] + offset);
		__asm__ volatile("fence iorw, iorw" ::: "memory");
		if ((sys_read32(c->base[bank] + offset) & mask) != (value & mask)) {
			return -EIO;
		}
	}
	d->values[bank][offset / 4] = value;
	d->masks[bank][offset / 4] |= mask;
	return 0;
}

static int set_asserted(const struct device *dev, uint32_t id, bool asserted)
{
	const struct a210_reset_line *line;

	if (id >= ARRAY_SIZE(reset_lines)) {
		return -EINVAL;
	}
	line = &reset_lines[id];
	return update_register(dev, line->bank, line->offset, BIT(line->bit), asserted);
}

static int assert_reset(const struct device *dev, uint32_t id)
{
	return set_asserted(dev, id, true);
}

static int deassert_reset(const struct device *dev, uint32_t id)
{
	return set_asserted(dev, id, false);
}

static int status(const struct device *dev, uint32_t id, uint8_t *asserted)
{
	const struct a210_reset_line *line;

	if (id >= ARRAY_SIZE(reset_lines)) {
		return -EINVAL;
	}
	line = &reset_lines[id];
	*asserted = !(read_register(dev, line->bank, line->offset) & BIT(line->bit));
	return 0;
}

int a210_reset_save_domain(const struct device *dev, unsigned int domain)
{
	const struct reset_config *c = dev->config;
	struct reset_data *d = dev->data;

	for (unsigned int i = 0; i < ARRAY_SIZE(reset_lines); i++) {
		const struct a210_reset_line *line = &reset_lines[i];
		unsigned int bank = line->bank;

		if (bank_domain(bank) != domain) {
			continue;
		}
		if (!accessible(c, bank)) {
			return -EACCES;
		}
		d->values[bank][line->offset / 4] = read_register(dev, bank, line->offset);
		d->masks[bank][line->offset / 4] |= BIT(line->bit);
	}
	return 0;
}

int a210_reset_restore_domain(const struct device *dev, unsigned int domain)
{
	const struct reset_config *c = dev->config;
	struct reset_data *d = dev->data;

	for (unsigned int bank = 0; bank < RESET_BANKS; bank++) {
		if (bank_domain(bank) != domain) {
			continue;
		}
		if (!accessible(c, bank)) {
			return -EACCES;
		}
		for (unsigned int word = 0; word < RESET_WORDS; word++) {
			uint32_t mask = d->masks[bank][word];
			uint32_t value;

			if (!mask) {
				continue;
			}
			value = (sys_read32(c->base[bank] + word * 4) & ~mask) |
				(d->values[bank][word] & mask);
			sys_write32(value, c->base[bank] + word * 4);
			__asm__ volatile("fence iorw, iorw" ::: "memory");
			if ((sys_read32(c->base[bank] + word * 4) & mask) != (value & mask)) {
				return -EIO;
			}
		}
	}
	return 0;
}

static DEVICE_API(reset, api) = {
	.status = status,
	.line_assert = assert_reset,
	.line_deassert = deassert_reset,
};

#define RESET_BASE(i, _) DT_INST_REG_ADDR_BY_IDX(0, i)
static const struct reset_config config = {
	.base = {LISTIFY(RESET_BANKS, RESET_BASE, (,), _)},
		 .gpu_status = DT_INST_PROP(0, gpu_status_reg),
		 .vo_status = DT_INST_PROP(0, vo_status_reg),
};
static struct reset_data data;
DEVICE_DT_INST_DEFINE(0, NULL, NULL, &data, &config, PRE_KERNEL_1, CONFIG_RESET_INIT_PRIORITY,
		      &api);
