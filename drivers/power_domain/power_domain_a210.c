/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT zhihe_a210_power_domain

#include <zephyr/drivers/clock_control/a210.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/power_domain/a210.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/reset/a210.h>
#include <zephyr/dt-bindings/power/zhihe-a210.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/sys_io.h>

#define PCU_SW_REQUEST       0x08
#define PCU_SW_STATE         0x0c
#define PCU_INTERRUPT_ENABLE 0x24
#define PCU_INTERRUPT_CLEAR  0x28
#define PCU_RAW_STATUS       0x2c
#define PCU_REQUEST          BIT(0)
#define PCU_STATE_OFF        0
#define PCU_REQUEST_DENIED   (BIT(1) | BIT(4))
#define PCU_REQUEST_TIMEOUT  (BIT(2) | BIT(5))
#define PCU_INTERRUPTS_ALL   GENMASK(5, 0)
#define PCU_TIMEOUT_MS       100

#define BPC_SW_CONTROL       0x00
#define BPC_SW_SLEEP_USER    0x04
#define BPC_HARDWARE_CONTROL 0
#define BPC_INITIAL_SIGNALS  0x10101

#define PCA_CONTROL 0x20

#define CCU_DELAY             0x00
#define CCU_DELAY_STEP_SHIFT  8
#define CCU_LOW_POWER         0x04
#define CCU_LOW_POWER_SCALE   2
#define CCU_LOW_POWER_GATE    4
#define CCU_LOW_POWER_DIVIDER 0x18

/* Cells of one zhihe,ccu-config tuple. */
enum {
	CCU_CONFIG_BASE,
	CCU_CONFIG_DELAY,
	CCU_CONFIG_STEP,
	CCU_CONFIG_SCALING,
	CCU_CONFIG_DIVIDER,
	CCU_CONFIG_CELLS,
};

#define IOPMP_GROUPS             64
#define IOPMP_WORDS              8
#define IOPMP_CONFIG(n)          ((n) * 4)
#define IOPMP_ADDRESS(n)         (0x800 + (n) * 0x20)
#define IOPMP_BASE_LOW           0x0
#define IOPMP_BASE_HIGH          0x4
#define IOPMP_LIMIT_LOW          0x8
#define IOPMP_LIMIT_HIGH         0xc
#define IOPMP_TOR_RWX_PAIR       0x0f0f
#define IOPMP_DEVICE_SHIFT       14
#define IOPMP_ADDRESS_LIMIT_HIGH 0x20
#define IOPMP_DEVICES            4

struct pd_config {
	uintptr_t pcu, bpc, pca, iopmp;
	const struct device *timer, *clocks, *resets, *supply, *parent;
	const uint16_t *clock_ids, *reset_ids;
	const uint32_t *ccus;
	const uint8_t *iopmp_devices;
	uint16_t clock_count, reset_count, ccu_count;
	uint8_t id, iopmp_count;
	bool always_on, retain_context;
};

struct pd_data {
	bool on, rail, faulted;
	uint8_t children;
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

static uint32_t pcu_state(const struct pd_config *c)
{
	return sys_read32(c->pcu + A210_PCU_CURRENT_STATE) & A210_PCU_STATE_MASK;
}

static int pcu_request(const struct pd_config *c, bool on)
{
	uint32_t state = on ? A210_PCU_STATE_ON : PCU_STATE_OFF;
	uint32_t start, status;

	if (!c->pcu || pcu_state(c) == state) {
		return 0;
	}
	if (c->pca && on) {
		write_reg(c->pca + PCA_CONTROL, 0);
	}
	if (c->bpc) {
		write_reg(c->bpc + BPC_SW_CONTROL, BPC_HARDWARE_CONTROL);
		write_reg(c->bpc + BPC_SW_SLEEP_USER, BPC_INITIAL_SIGNALS);
	}
	write_reg(c->pcu + PCU_INTERRUPT_CLEAR, PCU_INTERRUPTS_ALL);
	write_reg(c->pcu + PCU_INTERRUPT_ENABLE, PCU_INTERRUPTS_ALL);
	write_reg(c->pcu + PCU_SW_STATE, state);
	write_reg(c->pcu + PCU_SW_REQUEST, PCU_REQUEST);
	start = ticks(c);
	do {
		status = sys_read32(c->pcu + PCU_RAW_STATUS) & PCU_INTERRUPTS_ALL;
		if (status) {
			write_reg(c->pcu + PCU_INTERRUPT_CLEAR, status);
			if (status & PCU_REQUEST_DENIED) {
				return -EACCES;
			}
			if (status & PCU_REQUEST_TIMEOUT) {
				return -ETIMEDOUT;
			}
			return pcu_state(c) == state ? 0 : -EIO;
		}
	} while ((uint32_t)(ticks(c) - start) <
		 counter_get_frequency(c->timer) / (MSEC_PER_SEC / PCU_TIMEOUT_MS));
	return -ETIMEDOUT;
}

static void save_iopmp(const struct pd_config *c)
{
	if (c->id != A210_PD_VO || !c->iopmp) {
		return;
	}
	for (unsigned int group = 0; group < IOPMP_GROUPS; group++) {
		vo_context.config[group] = sys_read32(c->iopmp + IOPMP_CONFIG(group));
		for (unsigned int word = 0; word < IOPMP_WORDS; word++) {
			vo_context.address[group][word] = sys_read32(
				c->iopmp + IOPMP_ADDRESS(group) + word * sizeof(uint32_t));
		}
	}
	vo_context.valid = true;
}

static int restore_iopmp(const struct pd_config *c)
{
	if (c->id != A210_PD_VO || !c->iopmp || !vo_context.valid) {
		return 0;
	}
	for (unsigned int group = 0; group < IOPMP_GROUPS; group++) {
		write_reg(c->iopmp + IOPMP_CONFIG(group), 0);
		for (unsigned int word = 0; word < IOPMP_WORDS; word++) {
			uintptr_t reg = c->iopmp + IOPMP_ADDRESS(group) + word * sizeof(uint32_t);

			write_reg(reg, vo_context.address[group][word]);
			if (sys_read32(reg) != vo_context.address[group][word]) {
				return -EIO;
			}
		}
		write_reg(c->iopmp + IOPMP_CONFIG(group), vo_context.config[group]);
		if (sys_read32(c->iopmp + IOPMP_CONFIG(group)) != vo_context.config[group]) {
			return -EIO;
		}
	}
	return 0;
}

static bool iopmp_allows(uintptr_t iopmp, unsigned int group, uint32_t tag)
{
	uintptr_t addr = iopmp + IOPMP_ADDRESS(group);

	return sys_read32(iopmp + IOPMP_CONFIG(group)) == IOPMP_TOR_RWX_PAIR &&
	       !sys_read32(addr + IOPMP_BASE_LOW) && sys_read32(addr + IOPMP_BASE_HIGH) == tag &&
	       !sys_read32(addr + IOPMP_LIMIT_LOW) &&
	       sys_read32(addr + IOPMP_LIMIT_HIGH) == (tag | IOPMP_ADDRESS_LIMIT_HIGH);
}

/* Give each listed master a TOR pair covering [0, 2^39). */
static int configure_iopmp(const struct pd_config *c)
{
	unsigned int slots[IOPMP_DEVICES];

	for (unsigned int i = 0; i < c->iopmp_count; i++) {
		unsigned int free_slot = IOPMP_GROUPS;
		uint32_t tag = c->iopmp_devices[i] << IOPMP_DEVICE_SHIFT;

		slots[i] = IOPMP_GROUPS;
		for (unsigned int group = 0; group < IOPMP_GROUPS; group++) {
			bool taken = false;

			for (unsigned int j = 0; j < i; j++) {
				taken |= slots[j] == group;
			}
			if (taken) {
				continue;
			}
			if (!sys_read32(c->iopmp + IOPMP_CONFIG(group)) &&
			    free_slot == IOPMP_GROUPS) {
				free_slot = group;
			}
			if (iopmp_allows(c->iopmp, group, tag)) {
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
	for (unsigned int i = 0; i < c->iopmp_count; i++) {
		uintptr_t addr = c->iopmp + IOPMP_ADDRESS(slots[i]);
		uint32_t tag = c->iopmp_devices[i] << IOPMP_DEVICE_SHIFT;

		write_reg(c->iopmp + IOPMP_CONFIG(slots[i]), 0);
		write_reg(addr + IOPMP_BASE_LOW, 0);
		write_reg(addr + IOPMP_BASE_HIGH, tag);
		write_reg(addr + IOPMP_LIMIT_LOW, 0);
		write_reg(addr + IOPMP_LIMIT_HIGH, tag | IOPMP_ADDRESS_LIMIT_HIGH);
		write_reg(c->iopmp + IOPMP_CONFIG(slots[i]), IOPMP_TOR_RWX_PAIR);
		if (!iopmp_allows(c->iopmp, slots[i], tag)) {
			for (unsigned int j = 0; j <= i; j++) {
				write_reg(c->iopmp + IOPMP_CONFIG(slots[j]), 0);
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
		ret = configure_iopmp(c);
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
	if (!d->on || !c->pcu || pcu_state(c) == A210_PCU_STATE_ON) {
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

/* A parent domain stays on while any child domain is on. */
static int hold_parent(const struct device *dev, bool hold)
{
	const struct pd_config *c = dev->config;
	struct pd_data *p;
	int ret;

	if (!c->parent) {
		return 0;
	}
	p = c->parent->data;
	if (hold ? p->children++ > 0 : --p->children > 0) {
		return 0;
	}
	ret = a210_power_set_on(c->parent, hold);
	if (ret && hold) {
		p->children--;
	}
	return ret;
}

static int power_on(const struct device *dev)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;
	unsigned int held = 0;
	int ret = hold_parent(dev, true);

	if (ret) {
		return ret;
	}
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
	for (unsigned int i = 0; i < c->ccu_count; i += CCU_CONFIG_CELLS) {
		const uint32_t *ccu = &c->ccus[i];
		uintptr_t base = ccu[CCU_CONFIG_BASE];

		write_reg(base + CCU_DELAY,
			  ccu[CCU_CONFIG_DELAY] | (ccu[CCU_CONFIG_STEP] << CCU_DELAY_STEP_SHIFT));
		write_reg(base + CCU_LOW_POWER,
			  ccu[CCU_CONFIG_SCALING] ? CCU_LOW_POWER_SCALE : CCU_LOW_POWER_GATE);
		if (ccu[CCU_CONFIG_SCALING]) {
			write_reg(base + CCU_LOW_POWER_DIVIDER, ccu[CCU_CONFIG_DIVIDER]);
		}
	}
	d->on = true;
	return 0;
unwind:
	while (held) {
		d->faulted |= a210_clock_hold(c->clocks, c->clock_ids[--held], false) != 0;
	}
	d->faulted |= hold_parent(dev, false) != 0;
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
	if (d->children) {
		return -EBUSY;
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
	return hold_parent(dev, false);
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
	*on = c->pcu ? pcu_state(c) == A210_PCU_STATE_ON : d->on;
	return 0;
}

static int init(const struct device *dev)
{
	const struct pd_config *c = dev->config;
	struct pd_data *d = dev->data;

	if (!device_is_ready(c->clocks) || !device_is_ready(c->resets) ||
	    !device_is_ready(c->timer) || (c->supply && !device_is_ready(c->supply)) ||
	    !counter_get_frequency(c->timer) || c->ccu_count % CCU_CONFIG_CELLS) {
		return -ENODEV;
	}
	d->on = c->always_on || (c->pcu && pcu_state(c) == A210_PCU_STATE_ON);
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
#define PARENT(node)                                                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(node, power_domains), \
	(DEVICE_DT_GET(DT_PHANDLE(node, power_domains))), (NULL))
#define SUPPLY(node)                                                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(node, vin_supply), \
	(DEVICE_DT_GET(DT_PHANDLE(node, vin_supply))), (NULL))
#define DEFINE_PD(node)                                                                            \
	BUILD_ASSERT(DT_PROP(node, domain_id) < ARRAY_SIZE(domains),                               \
		     "A210 power domain IDs must be dense");                                       \
	BUILD_ASSERT(DT_PROP_LEN_OR(node, zhihe_iopmp_devices, 0) <= IOPMP_DEVICES &&              \
		     (!DT_NODE_HAS_PROP(node, zhihe_iopmp_devices) ||                              \
		      DT_REG_HAS_NAME(node, iopmp)),                                               \
		     "A210 IOPMP devices need an iopmp region");                                   \
	static const uint16_t clocks_##node[] = {                                                  \
		0, COND_CODE_1(DT_NODE_HAS_PROP(node, clocks), \
			(DT_FOREACH_PROP_ELEM(node, clocks, CLOCK_ID)), ())}; \
	static const uint16_t resets_##node[] = {                                                  \
		0, COND_CODE_1(DT_NODE_HAS_PROP(node, resets), \
			(DT_FOREACH_PROP_ELEM(node, resets, RESET_ID)), ())}; \
	static const uint32_t ccus_##node[] = DT_PROP_OR(node, zhihe_ccu_config, {0});             \
	static const uint8_t iopmp_##node[] = DT_PROP_OR(node, zhihe_iopmp_devices, {0});          \
	static const struct pd_config config_##node = {                                            \
		.pcu = DT_REG_ADDR_BY_NAME_OR(node, pcu, 0),                                       \
		.bpc = DT_REG_ADDR_BY_NAME_OR(node, bpc, 0),                                       \
		.pca = DT_REG_ADDR_BY_NAME_OR(node, pca, 0),                                       \
		.iopmp = DT_REG_ADDR_BY_NAME_OR(node, iopmp, 0),                                   \
		.timer = DEVICE_DT_GET(DT_PHANDLE(node, timeout_counter)),                         \
		.clocks = DEVICE_DT_GET(DT_PHANDLE(node, zhihe_clock_controller)),                 \
		.resets = DEVICE_DT_GET(DT_PHANDLE(node, zhihe_reset_controller)),                 \
		.supply = SUPPLY(node),                                                            \
		.parent = PARENT(node),                                                            \
		.id = DT_PROP(node, domain_id),                                                    \
		.always_on = DT_PROP(node, always_on),                                             \
		.retain_context = DT_PROP(node, zhihe_retain_context),                             \
		.clock_ids = clocks_##node + 1,                                                    \
		.clock_count = ARRAY_SIZE(clocks_##node) - 1,                                      \
		.reset_ids = resets_##node + 1,                                                    \
		.reset_count = ARRAY_SIZE(resets_##node) - 1,                                      \
		.ccus = ccus_##node,                                                               \
		.ccu_count = DT_PROP_LEN_OR(node, zhihe_ccu_config, 0),                            \
		.iopmp_devices = iopmp_##node,                                                     \
		.iopmp_count = DT_PROP_LEN_OR(node, zhihe_iopmp_devices, 0),                       \
	};                                                                                         \
	static struct pd_data data_##node;                                                         \
	PM_DEVICE_DT_DEFINE(node, pm_action);                                                      \
	DEVICE_DT_DEFINE(node, init, PM_DEVICE_DT_GET(node), &data_##node, &config_##node,         \
			 POST_KERNEL, CONFIG_POWER_DOMAIN_A210_INIT_PRIORITY, NULL);

DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, DEFINE_PD)
