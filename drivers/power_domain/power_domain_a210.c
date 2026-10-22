/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT zhihe_a210_power_domain

#include <zephyr/drivers/clock_control/a210.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/power_domain/a210.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/reset/a210.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/sys_io.h>

#define PCU_REQUEST      0x08
#define PCU_TARGET       0x0c
#define PCU_IRQ_ENABLE   0x24
#define PCU_IRQ_CLEAR    0x28
#define PCU_IRQ_STATUS   0x2c
#define PCU_STATE        0x48
#define DOMAIN_ON        0x1f
#define GPU_DOMAIN       1
#define VO_DOMAIN        2
#define IOPMP_GROUPS     64
#define IOPMP_WORDS      8
#define IOPMP_ADDRESS(n) (0x800 + (n) * 0x20)

struct pd_config {
	uintptr_t pcu, bpc, pca, iopmp;
	const struct device *timer, *clocks, *resets, *supply;
	const uint16_t *clock_ids, *reset_ids;
	const uint32_t *ccus;
	uint16_t clock_count, reset_count, ccu_count;
	uint8_t id;
	bool always_on, retain_context;
};

struct pd_data {
	bool on, rail, faulted;
};

static const struct device *domains[DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)];

static struct {
	uint32_t config[IOPMP_GROUPS];
	uint32_t address[IOPMP_GROUPS][IOPMP_WORDS];
	bool valid;
} vo_context;

static void write_reg(uintptr_t reg, uint32_t value)
{
	sys_write32(value, reg);
	__asm__ volatile("fence iorw, iorw" ::: "memory");
}

static uint32_t ticks(const struct pd_config *c)
{
	uint32_t value = 0;

	counter_get_value(c->timer, &value);
	return value;
}

static int pcu_request(const struct pd_config *c, bool on)
{
	uint32_t state = on ? DOMAIN_ON : 0;
	uint32_t start, status;

	if (!c->pcu || (sys_read32(c->pcu + PCU_STATE) & 0x1f) == state) {
		return 0;
	}
	if (c->pca && on) {
		write_reg(c->pca + 0x20, 0);
	}
	if (c->bpc) {
		write_reg(c->bpc, 0);
		write_reg(c->bpc + 4, 0x10101);
	}
	write_reg(c->pcu + PCU_IRQ_CLEAR, 0x3f);
	write_reg(c->pcu + PCU_IRQ_ENABLE, 0x3f);
	write_reg(c->pcu + PCU_TARGET, state);
	write_reg(c->pcu + PCU_REQUEST, 1);
	start = ticks(c);
	do {
		status = sys_read32(c->pcu + PCU_IRQ_STATUS) & 0x3f;
		if (status) {
			write_reg(c->pcu + PCU_IRQ_CLEAR, status);
			if (status & (BIT(1) | BIT(4))) {
				return -EACCES;
			}
			if (status & (BIT(2) | BIT(5))) {
				return -ETIMEDOUT;
			}
			return (sys_read32(c->pcu + PCU_STATE) & 0x1f) == state ? 0 : -EIO;
		}
	} while ((uint32_t)(ticks(c) - start) < counter_get_frequency(c->timer) / 10);
	return -ETIMEDOUT;
}

static void save_iopmp(const struct pd_config *c)
{
	if (c->id != VO_DOMAIN || !c->iopmp) {
		return;
	}
	for (unsigned int group = 0; group < IOPMP_GROUPS; group++) {
		vo_context.config[group] = sys_read32(c->iopmp + group * 4);
		for (unsigned int word = 0; word < IOPMP_WORDS; word++) {
			vo_context.address[group][word] =
				sys_read32(c->iopmp + IOPMP_ADDRESS(group) + word * 4);
		}
	}
	vo_context.valid = true;
}

static int restore_iopmp(const struct pd_config *c)
{
	if (c->id != VO_DOMAIN || !c->iopmp || !vo_context.valid) {
		return 0;
	}
	for (unsigned int group = 0; group < IOPMP_GROUPS; group++) {
		write_reg(c->iopmp + group * 4, 0);
		for (unsigned int word = 0; word < IOPMP_WORDS; word++) {
			uintptr_t reg = c->iopmp + IOPMP_ADDRESS(group) + word * 4;

			write_reg(reg, vo_context.address[group][word]);
			if (sys_read32(reg) != vo_context.address[group][word]) {
				return -EIO;
			}
		}
		write_reg(c->iopmp + group * 4, vo_context.config[group]);
		if (sys_read32(c->iopmp + group * 4) != vo_context.config[group]) {
			return -EIO;
		}
	}
	return 0;
}

static int configure_gpu_iopmp(const struct pd_config *c)
{
	unsigned int slots[2];

	if (c->id != GPU_DOMAIN || !c->iopmp) {
		return 0;
	}
	for (unsigned int i = 0; i < 2; i++) {
		unsigned int free_slot = IOPMP_GROUPS;
		uint32_t tag = (0x33 + i) << 14;

		slots[i] = IOPMP_GROUPS;
		for (unsigned int group = 0; group < IOPMP_GROUPS; group++) {
			uintptr_t addr = c->iopmp + IOPMP_ADDRESS(group);
			uint32_t config = sys_read32(c->iopmp + group * 4);

			if (i && group == slots[0]) {
				continue;
			}
			if (!config && free_slot == IOPMP_GROUPS) {
				free_slot = group;
			}
			if (config == 0x0f0f && !sys_read32(addr) && !sys_read32(addr + 8) &&
			    sys_read32(addr + 4) == tag && sys_read32(addr + 12) == (tag | 0x20)) {
				slots[i] = group;
				break;
			}
		}
		if (slots[i] == IOPMP_GROUPS) {
			slots[i] = free_slot;
		}
		if (slots[i] == IOPMP_GROUPS) {
			return -ENOSPC;
		}
	}
	for (unsigned int i = 0; i < 2; i++) {
		uintptr_t addr = c->iopmp + IOPMP_ADDRESS(slots[i]);
		uint32_t tag = (0x33 + i) << 14;

		write_reg(c->iopmp + slots[i] * 4, 0);
		write_reg(addr, 0);
		write_reg(addr + 4, tag);
		write_reg(addr + 8, 0);
		write_reg(addr + 12, tag | 0x20);
		write_reg(c->iopmp + slots[i] * 4, 0x0f0f);
		if (sys_read32(c->iopmp + slots[i] * 4) != 0x0f0f || sys_read32(addr) ||
		    sys_read32(addr + 4) != tag || sys_read32(addr + 8) ||
		    sys_read32(addr + 12) != (tag | 0x20)) {
			for (unsigned int j = 0; j <= i; j++) {
				write_reg(c->iopmp + slots[j] * 4, 0);
			}
			return -EIO;
		}
	}
	return 0;
}

static int reset_domain(const struct pd_config *c, bool asserted)
{
	for (unsigned int i = 0; i < c->reset_count; i++) {
		int ret = asserted ? reset_line_assert(c->resets, c->reset_ids[i])
				   : reset_line_deassert(c->resets, c->reset_ids[i]);

		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int switch_power(const struct device *dev, bool on)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;
	int ret;

	if (on && c->supply && !d->rail) {
		ret = regulator_enable(c->supply);
		if (ret) {
			return ret;
		}
		d->rail = true;
	}
	ret = pcu_request(c, on);
	if (ret) {
		return ret;
	}
	if (!on && c->supply && d->rail) {
		ret = regulator_disable(c->supply);
		if (ret) {
			return ret;
		}
		d->rail = false;
	}
	return 0;
}

static int restore_context(const struct pd_config *c)
{
	int ret = a210_clock_restore_domain(c->clocks, c->id);

	if (!ret) {
		ret = a210_reset_restore_domain(c->resets, c->id);
	}
	if (!ret) {
		ret = reset_domain(c, false);
	}
	if (!ret) {
		ret = restore_iopmp(c);
	}
	if (!ret) {
		ret = configure_gpu_iopmp(c);
	}
	return ret;
}

static int reconcile(const struct device *dev)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;
	int ret;

	if (d->faulted) {
		return -EIO;
	}
	if (!d->on || !c->pcu || (sys_read32(c->pcu + PCU_STATE) & 0x1f) == DOMAIN_ON) {
		return 0;
	}
	/* A shared PHY reset can remove an already-requested domain. */
	ret = switch_power(dev, true);
	if (!ret) {
		ret = restore_context(c);
	}
	d->faulted = ret != 0;
	return ret;
}

int a210_power_set_on(const struct device *dev, bool on)
{
	struct pd_data *d = dev->data;
	int ret;

	if (d->faulted) {
		return -EIO;
	}
	if (on && d->on) {
		return reconcile(dev);
	}
	ret = pm_device_action_run(dev, on ? PM_DEVICE_ACTION_RESUME : PM_DEVICE_ACTION_SUSPEND);
	return ret == -EALREADY ? 0 : ret;
}

int a210_power_sync_clock(const struct device *clock, unsigned int id)
{
	for (unsigned int n = 0; n < ARRAY_SIZE(domains); n++) {
		const struct device *dev = domains[n];
		const struct pd_config *c;
		struct pd_data *d;

		if (!dev) {
			continue;
		}
		c = dev->config;
		d = dev->data;
		if (c->clocks != clock || !c->pcu || !d->on) {
			continue;
		}
		for (unsigned int i = 0; i < c->clock_count; i++) {
			if (c->clock_ids[i] == id) {
				int ret = reconcile(dev);

				if (ret) {
					return ret;
				}
				break;
			}
		}
	}
	return 0;
}

static int power_on(const struct device *dev)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;
	unsigned int held = 0;
	int ret = 0;

	for (; held < c->clock_count; held++) {
		ret = a210_clock_hold(c->clocks, c->clock_ids[held], true);
		if (ret) {
			goto unwind;
		}
	}
	ret = switch_power(dev, true);
	if (!ret) {
		ret = restore_context(c);
	}
	if (ret) {
		d->faulted |= switch_power(dev, false) != 0;
		goto unwind;
	}
	for (unsigned int i = 0; i < c->ccu_count; i += 5) {
		uintptr_t base = c->ccus[i];

		write_reg(base, c->ccus[i + 1] | (c->ccus[i + 2] << 8));
		write_reg(base + 4, c->ccus[i + 3] ? 2 : 4);
		if (c->ccus[i + 3]) {
			write_reg(base + 0x18, c->ccus[i + 4]);
		}
	}
	d->on = true;
	return 0;
unwind:
	while (held) {
		d->faulted |= a210_clock_hold(c->clocks, c->clock_ids[--held], false) != 0;
	}
	return ret;
}

static int power_off(const struct device *dev)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;
	int ret;

	if (c->always_on) {
		return -EACCES;
	}
	ret = a210_clock_save_domain(c->clocks, c->id);
	if (!ret) {
		ret = a210_reset_save_domain(c->resets, c->id);
	}
	if (ret) {
		return ret;
	}
	save_iopmp(c);
	ret = c->retain_context ? 0 : reset_domain(c, true);
	if (!ret) {
		ret = switch_power(dev, false);
	}
	if (ret) {
		d->faulted |= switch_power(dev, true) != 0 || restore_context(c) != 0;
		return ret;
	}
	for (unsigned int i = c->clock_count; i > 0; i--) {
		ret = a210_clock_hold(c->clocks, c->clock_ids[i - 1], false);
		if (ret) {
			d->faulted = true;
			return ret;
		}
	}
	d->on = false;
	return 0;
}

static int pm_action(const struct device *dev, enum pm_device_action action)
{
	struct pd_data *d = dev->data;

	if (d->faulted) {
		return -EIO;
	}
	if (action == PM_DEVICE_ACTION_RESUME) {
		return power_on(dev);
	}
	if (action == PM_DEVICE_ACTION_SUSPEND) {
		return power_off(dev);
	}
	return -ENOTSUP;
}

int a210_power_get_on(const struct device *dev, bool *on)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;

	if (d->faulted) {
		return -EIO;
	}
	*on = c->pcu ? (sys_read32(c->pcu + PCU_STATE) & 0x1f) == DOMAIN_ON : d->on;
	return 0;
}

static int init(const struct device *dev)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;

	if (!device_is_ready(c->clocks) || !device_is_ready(c->resets) ||
	    !device_is_ready(c->timer) || (c->supply && !device_is_ready(c->supply)) ||
	    !counter_get_frequency(c->timer) || c->ccu_count % 5) {
		return -ENODEV;
	}
	d->on = c->always_on || (c->pcu && (sys_read32(c->pcu + PCU_STATE) & 0x1f) == DOMAIN_ON);
	save_iopmp(c);
	if (d->on && !c->always_on) {
		int ret = power_on(dev);

		if (ret) {
			return ret;
		}
	}
	if (!d->on) {
		pm_device_init_suspended(dev);
	}
	domains[c->id] = dev;
	return 0;
}

#define CLOCK_ID(node, prop, i) DT_CLOCKS_CELL_BY_IDX(node, i, id),
#define RESET_ID(node, prop, i) DT_RESET_ID_BY_IDX(node, i),
#define SUPPLY(node)                                                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(node, vin_supply), \
	(DEVICE_DT_GET(DT_PHANDLE(node, vin_supply))), (NULL))
#define DEFINE_PD(node)                                                                            \
	BUILD_ASSERT(DT_PROP(node, domain_id) < ARRAY_SIZE(domains),                               \
		     "A210 power domain IDs must be dense");                                       \
	static const uint16_t clocks_##node[] = {                                                  \
		0, COND_CODE_1(DT_NODE_HAS_PROP(node, clocks), \
			(DT_FOREACH_PROP_ELEM(node, clocks, CLOCK_ID)), ())}; \
	static const uint16_t resets_##node[] = {                                                  \
		0, COND_CODE_1(DT_NODE_HAS_PROP(node, resets), \
			(DT_FOREACH_PROP_ELEM(node, resets, RESET_ID)), ())}; \
	static const uint32_t ccus_##node[] = DT_PROP_OR(node, zhihe_ccu_config, {0});             \
	static const struct pd_config config_##node = {                                            \
		.pcu = DT_REG_ADDR_BY_NAME_OR(node, pcu, 0),                                       \
		.bpc = DT_REG_ADDR_BY_NAME_OR(node, bpc, 0),                                       \
		.pca = DT_REG_ADDR_BY_NAME_OR(node, pca, 0),                                       \
		.iopmp = DT_REG_ADDR_BY_NAME_OR(node, iopmp, 0),                                   \
		.timer = DEVICE_DT_GET(DT_PHANDLE(node, timeout_counter)),                         \
		.clocks = DEVICE_DT_GET(DT_PHANDLE(node, zhihe_clock_controller)),                 \
		.resets = DEVICE_DT_GET(DT_PHANDLE(node, zhihe_reset_controller)),                 \
		.supply = SUPPLY(node),                                                            \
		.id = DT_PROP(node, domain_id),                                                    \
		.always_on = DT_PROP(node, always_on),                                             \
		.retain_context = DT_PROP(node, zhihe_retain_context),                             \
		.clock_ids = clocks_##node + 1,                                                    \
		.clock_count = ARRAY_SIZE(clocks_##node) - 1,                                      \
		.reset_ids = resets_##node + 1,                                                    \
		.reset_count = ARRAY_SIZE(resets_##node) - 1,                                      \
		.ccus = ccus_##node,                                                               \
		.ccu_count = DT_PROP_LEN_OR(node, zhihe_ccu_config, 0),                            \
	};                                                                                         \
	static struct pd_data data_##node;                                                         \
	PM_DEVICE_DT_DEFINE(node, pm_action);                                                      \
	DEVICE_DT_DEFINE(node, init, PM_DEVICE_DT_GET(node), &data_##node, &config_##node,         \
			 POST_KERNEL, CONFIG_POWER_DOMAIN_A210_INIT_PRIORITY, NULL);

DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, DEFINE_PD)
