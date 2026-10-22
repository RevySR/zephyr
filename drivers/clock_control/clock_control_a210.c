/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT zhihe_a210_clock_tree

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/a210.h>
#include <zephyr/drivers/counter.h>
#if defined(CONFIG_POWER_DOMAIN_A210)
#include <zephyr/drivers/power_domain/a210.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include "a210_clock_tree.h"
#include "a210_clock_data.h"

#define CLOCK_BANKS 13
#define GPU_DOMAIN  1
#define VO_DOMAIN   2

struct tree_config {
	uintptr_t base[CLOCK_BANKS];
	uintptr_t gpu_status;
	uintptr_t vo_status;
	const struct device *timer;
	uint32_t oscillator;
	uint32_t rtc;
	uint32_t aon_rates[4];
	const uint32_t *boot_parents;
	unsigned int boot_parent_count;
	const uint32_t *boot_rates;
	unsigned int boot_rate_count;
};

struct tree_data {
	struct a210_clock_tree tree;
	uint16_t users[ARRAY_SIZE(clock_descs)];
	bool enabled[ARRAY_SIZE(clock_map)];
	uint32_t values[ARRAY_SIZE(clock_words)];
	uint32_t masks[ARRAY_SIZE(clock_words)];
	uint32_t last_ticks;
	uint64_t total_ticks;
};

static unsigned int bank_domain(unsigned int bank)
{
	return bank == 2 ? GPU_DOMAIN : (bank == 5 || bank == 6 ? VO_DOMAIN : 0);
}

static bool accessible(const struct tree_config *c, unsigned int bank)
{
	unsigned int domain = bank_domain(bank);
	uintptr_t status = domain == GPU_DOMAIN ? c->gpu_status : c->vo_status;

	return !domain || (sys_read32(status) & 0x1f);
}

static int word_index(unsigned int bank, unsigned int offset)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_words); i++) {
		if (clock_words[i].bank == bank && clock_words[i].offset == offset) {
			return i;
		}
	}
	return -ENOENT;
}

static uint32_t read_register(void *context, unsigned int bank, unsigned int offset)
{
	const struct device *dev = context;
	const struct tree_config *c = dev->config;
	struct tree_data *d = dev->data;
	int index = word_index(bank, offset);

	if (accessible(c, bank)) {
		return sys_read32(c->base[bank] + offset);
	}
	return index < 0 ? 0 : d->values[index];
}

static int update_register(void *context, unsigned int bank, unsigned int offset, uint32_t mask,
			   uint32_t value)
{
	const struct device *dev = context;
	const struct tree_config *c = dev->config;
	struct tree_data *d = dev->data;
	int index = word_index(bank, offset);
	uint32_t reg = read_register(context, bank, offset);

	reg = (reg & ~mask) | (value & mask);
	if (accessible(c, bank)) {
		sys_write32(reg, c->base[bank] + offset);
		__asm__ volatile("fence iorw, iorw" ::: "memory");
		if ((sys_read32(c->base[bank] + offset) & mask) != (reg & mask)) {
			return -EIO;
		}
	}
	if (index >= 0) {
		d->values[index] = reg;
		d->masks[index] |= mask;
	}
	return 0;
}

static uint32_t time_us(void *context)
{
	const struct device *dev = context;
	const struct tree_config *c = dev->config;
	struct tree_data *d = dev->data;
	uint32_t ticks;

	counter_get_value(c->timer, &ticks);
	d->total_ticks += (uint32_t)(ticks - d->last_ticks);
	d->last_ticks = ticks;
	return d->total_ticks / (counter_get_frequency(c->timer) / 1000000);
}

static void delay_us(void *context, uint32_t us)
{
	uint32_t start = time_us(context);

	while ((uint32_t)(time_us(context) - start) < us) {
	}
}

static int clock_on(const struct device *dev, clock_control_subsys_t sys)
{
	struct tree_data *d = dev->data;
	uintptr_t id = (uintptr_t)sys;
	int ret;

	if (id >= ARRAY_SIZE(clock_map)) {
		return -EINVAL;
	}
#if defined(CONFIG_POWER_DOMAIN_A210)
	ret = a210_power_sync_clock(dev, id);
	if (ret) {
		return ret;
	}
#endif
	if (d->enabled[id]) {
		return 0;
	}
	ret = a210_tree_enable(&d->tree, clock_map[id]);
	if (!ret) {
		d->enabled[id] = true;
	}
	return ret;
}

static int clock_off(const struct device *dev, clock_control_subsys_t sys)
{
	struct tree_data *d = dev->data;
	uintptr_t id = (uintptr_t)sys;
	int ret;

	if (id >= ARRAY_SIZE(clock_map)) {
		return -EINVAL;
	}
	if (!d->enabled[id]) {
		return 0;
	}
	ret = a210_tree_disable(&d->tree, clock_map[id]);
	if (!ret) {
		d->enabled[id] = false;
	}
	return ret;
}

static int get_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *rate)
{
	struct tree_data *d = dev->data;
	uintptr_t id = (uintptr_t)sys;

	return id < ARRAY_SIZE(clock_map) ? a210_tree_rate(&d->tree, clock_map[id], rate) : -EINVAL;
}

static int set_rate(const struct device *dev, clock_control_subsys_t sys,
		    clock_control_subsys_rate_t rate)
{
	struct tree_data *d = dev->data;
	uintptr_t id = (uintptr_t)sys;

	return id < ARRAY_SIZE(clock_map)
		       ? a210_tree_set_rate(&d->tree, clock_map[id], (uintptr_t)rate)
		       : -EINVAL;
}

static enum clock_control_status get_status(const struct device *dev, clock_control_subsys_t sys)
{
	struct tree_data *d = dev->data;
	uintptr_t id = (uintptr_t)sys;
	bool enabled;

	if (id >= ARRAY_SIZE(clock_map) || a210_tree_state(&d->tree, clock_map[id], &enabled)) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}
	return enabled ? CLOCK_CONTROL_STATUS_ON : CLOCK_CONTROL_STATUS_OFF;
}

int a210_clock_rate_count(const struct device *dev, unsigned int id)
{
	struct tree_data *d = dev->data;

	return id < ARRAY_SIZE(clock_map) ? a210_tree_rate_count(&d->tree, clock_map[id]) : -EINVAL;
}

int a210_clock_hold(const struct device *dev, unsigned int id, bool hold)
{
	struct tree_data *d = dev->data;

	if (id >= ARRAY_SIZE(clock_map)) {
		return -EINVAL;
	}
	return hold ? a210_tree_enable(&d->tree, clock_map[id])
		    : a210_tree_disable(&d->tree, clock_map[id]);
}

int a210_clock_describe(const struct device *dev, unsigned int id, unsigned int index,
			uint32_t *rate)
{
	struct tree_data *d = dev->data;

	return id < ARRAY_SIZE(clock_map) ? a210_tree_describe(&d->tree, clock_map[id], index, rate)
					  : -EINVAL;
}

int a210_clock_save_domain(const struct device *dev, unsigned int domain)
{
	const struct tree_config *c = dev->config;
	struct tree_data *d = dev->data;

	for (unsigned int i = 0; i < ARRAY_SIZE(clock_words); i++) {
		const struct a210_clock_word *w = &clock_words[i];

		if (bank_domain(w->bank) == domain && w->mask) {
			if (!accessible(c, w->bank)) {
				return -EACCES;
			}
			d->values[i] = sys_read32(c->base[w->bank] + w->offset);
			d->masks[i] = w->mask;
		}
	}
	return 0;
}

int a210_clock_restore_domain(const struct device *dev, unsigned int domain)
{
	const struct tree_config *c = dev->config;
	struct tree_data *d = dev->data;

	for (unsigned int i = 0; i < ARRAY_SIZE(clock_words); i++) {
		const struct a210_clock_word *w = &clock_words[i];
		uintptr_t reg = c->base[w->bank] + w->offset;
		uint32_t value;

		if (bank_domain(w->bank) != domain || !d->masks[i]) {
			continue;
		}
		if (!accessible(c, w->bank)) {
			return -EACCES;
		}
		value = (sys_read32(reg) & ~d->masks[i]) | (d->values[i] & d->masks[i]);
		if (w->sync) {
			sys_write32(value & ~w->sync, reg);
			delay_us((void *)dev, 1);
		}
		sys_write32(value, reg);
		__asm__ volatile("fence iorw, iorw" ::: "memory");
		if ((sys_read32(reg) & d->masks[i]) != (value & d->masks[i])) {
			return -EIO;
		}
	}
	return 0;
}

static int init(const struct device *dev)
{
	const struct tree_config *c = dev->config;
	struct tree_data *d = dev->data;
	int ret;

	if (!device_is_ready(c->timer) || counter_get_frequency(c->timer) < 1000000) {
		return -ENODEV;
	}
	d->tree = (struct a210_clock_tree){
		.clocks = clock_descs,
		.plls = clock_plls,
		.parents = clock_parents,
		.count = ARRAY_SIZE(clock_descs),
		.users = d->users,
		.oscillator = c->oscillator,
		.rtc = c->rtc,
		.context = (void *)dev,
		.read = read_register,
		.update = update_register,
		.time_us = time_us,
		.delay_us = delay_us,
	};
	memcpy(d->tree.aon_rates, c->aon_rates, sizeof(c->aon_rates));
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_critical); i++) {
		ret = a210_tree_enable(&d->tree, clock_critical[i]);
		if (ret) {
			return ret;
		}
	}
	if (c->boot_rate_count % 2) {
		return -EINVAL;
	}
	for (unsigned int i = 0; i < c->boot_rate_count; i += 2) {
		unsigned int clock = c->boot_rates[i];

		if (clock >= ARRAY_SIZE(clock_map)) {
			return -EINVAL;
		}
		ret = a210_tree_set_rate(&d->tree, clock_map[clock], c->boot_rates[i + 1]);
		if (ret) {
			return ret;
		}
	}
	if (c->boot_parent_count % 2) {
		return -EINVAL;
	}
	for (unsigned int i = 0; i < c->boot_parent_count; i += 2) {
		unsigned int clock = c->boot_parents[i];
		unsigned int parent = c->boot_parents[i + 1];

		if (clock >= ARRAY_SIZE(clock_map) || parent >= ARRAY_SIZE(clock_map)) {
			return -EINVAL;
		}
		ret = a210_tree_set_parent(&d->tree, clock_map[clock], clock_map[parent]);
		if (ret) {
			return ret;
		}
	}
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_map); i++) {
		bool enabled;

		ret = a210_tree_state(&d->tree, clock_map[i], &enabled);
		if (ret) {
			return ret;
		}
		if (enabled) {
			ret = clock_on(dev, (void *)(uintptr_t)i);
			if (ret) {
				return ret;
			}
		}
	}
	return 0;
}

static DEVICE_API(clock_control, api) = {
	.on = clock_on,
	.off = clock_off,
	.get_rate = get_rate,
	.set_rate = set_rate,
	.get_status = get_status,
};

#define CLOCK_BASE(i, _) DT_INST_REG_ADDR_BY_IDX(0, i)
static const uint32_t boot_rates[] = DT_INST_PROP_OR(0, zhihe_boot_rates, {0});
static const uint32_t boot_parents[] = DT_INST_PROP_OR(0, zhihe_boot_parents, {0});
static const struct tree_config config = {
	.base = {LISTIFY(CLOCK_BANKS, CLOCK_BASE, (,), _)},
		 .gpu_status = DT_INST_PROP(0, gpu_status_reg),
		 .vo_status = DT_INST_PROP(0, vo_status_reg),
		 .timer = DEVICE_DT_GET(DT_INST_PHANDLE(0, timeout_counter)),
		 .oscillator = DT_INST_PROP(0, clock_frequency),
		 .rtc = DT_INST_PROP(0, rtc_frequency),
		 .aon_rates = DT_INST_PROP(0, aon_source_frequencies), .boot_parents = boot_parents,
		 .boot_parent_count = DT_INST_PROP_LEN_OR(0, zhihe_boot_parents, 0),
		 .boot_rates = boot_rates,
		 .boot_rate_count = DT_INST_PROP_LEN_OR(0, zhihe_boot_rates, 0),
};
static struct tree_data data;
DEVICE_DT_INST_DEFINE(0, init, NULL, &data, &config, POST_KERNEL,
		      CONFIG_CLOCK_CONTROL_A210_INIT_PRIORITY, &api);
