/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT zhihe_a210_reset

#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/power_domain/a210.h>
#include <zephyr/drivers/reset/a210.h>
#include <zephyr/dt-bindings/power/zhihe-a210.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* Register banks, matching the reset controller reg-names. */
enum reset_bank {
	BANK_GPU,
	BANK_PCIE,
	BANK_PERI0,
	BANK_PERI1,
	BANK_PERI2,
	BANK_PERI3,
	BANK_TEE,
	BANK_USB,
	BANK_VO,
	BANK_AON,
	RESET_BANKS,
};

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
	switch (bank) {
	case BANK_GPU:
		return A210_PD_GPU;
	case BANK_VO:
		return A210_PD_VO;
	default:
		return A210_PD_TOP;
	}
}

static bool accessible(const struct reset_config *c, unsigned int bank)
{
	unsigned int domain = bank_domain(bank);
	uintptr_t status = domain == A210_PD_GPU ? c->gpu_status : c->vo_status;

	return domain == A210_PD_TOP || (sys_read32(status) & A210_PCU_STATE_MASK);
}

static uint32_t read_register(const struct device *dev, unsigned int bank, unsigned int offset)
{
	const struct reset_config *c = dev->config;
	struct reset_data *d = dev->data;

	return accessible(c, bank) ? sys_read32(c->base[bank] + offset)
				   : d->values[bank][offset / sizeof(uint32_t)];
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
	d->values[bank][offset / sizeof(uint32_t)] = value;
	d->masks[bank][offset / sizeof(uint32_t)] |= mask;
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
		d->values[bank][line->offset / sizeof(uint32_t)] =
			read_register(dev, bank, line->offset);
		d->masks[bank][line->offset / sizeof(uint32_t)] |= BIT(line->bit);
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
			uintptr_t reg = c->base[bank] + word * sizeof(uint32_t);
			uint32_t mask = d->masks[bank][word];
			uint32_t value;

			if (!mask) {
				continue;
			}
			value = (sys_read32(reg) & ~mask) | (d->values[bank][word] & mask);
			sys_write32(value, reg);
			__asm__ volatile("fence iorw, iorw" ::: "memory");
			if ((sys_read32(reg) & mask) != (value & mask)) {
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

static const struct reset_config config = {
	.base =
		{
			[BANK_GPU] = DT_INST_REG_ADDR_BY_NAME(0, gpu),
			[BANK_PCIE] = DT_INST_REG_ADDR_BY_NAME(0, pcie),
			[BANK_PERI0] = DT_INST_REG_ADDR_BY_NAME(0, peri0),
			[BANK_PERI1] = DT_INST_REG_ADDR_BY_NAME(0, peri1),
			[BANK_PERI2] = DT_INST_REG_ADDR_BY_NAME(0, peri2),
			[BANK_PERI3] = DT_INST_REG_ADDR_BY_NAME(0, peri3),
			[BANK_TEE] = DT_INST_REG_ADDR_BY_NAME(0, tee),
			[BANK_USB] = DT_INST_REG_ADDR_BY_NAME(0, usb),
			[BANK_VO] = DT_INST_REG_ADDR_BY_NAME(0, vo),
			[BANK_AON] = DT_INST_REG_ADDR_BY_NAME(0, aon),
		},
	.gpu_status = DT_INST_PROP(0, gpu_status_reg),
	.vo_status = DT_INST_PROP(0, vo_status_reg),
};
static struct reset_data data;
DEVICE_DT_INST_DEFINE(0, NULL, NULL, &data, &config, PRE_KERNEL_1, CONFIG_RESET_INIT_PRIORITY,
		      &api);
