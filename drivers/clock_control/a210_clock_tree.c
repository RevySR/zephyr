/* SPDX-License-Identifier: Apache-2.0 */
#include "a210_clock_tree.h"
#include <zephyr/sys/util.h>
#include <errno.h>

#define PROPAGATE       BIT(0)
#define CDE_DIVISOR     BIT(1)
#define PLL_RESET       BIT(30)
#define PLL_BYPASS      BIT(31)
#define PLL_INTEGER     (BIT(24) | BIT(25))
#define PLL_CONFIG_MASK (GENMASK(26, 24) | GENMASK(22, 20) | GENMASK(19, 8) | GENMASK(5, 0))

static uint32_t reg(const struct a210_clock_tree *t, const struct a210_tree_clock *c,
		    unsigned int offset)
{
	return t->read(t->context, c->bank, c->offset + offset);
}

static uint32_t field_mask(const struct a210_tree_clock *c)
{
	return BIT(c->width) - 1;
}

static int parent(const struct a210_clock_tree *t, const struct a210_tree_clock *c)
{
	if (c->kind == A210_MUX) {
		unsigned int select = (reg(t, c, 0) >> c->shift) & field_mask(c);

		return select < c->arg1 ? t->parents[c->arg0 + select] : -EIO;
	}
	return c->parent;
}

static uint32_t pll_rate(uint32_t reference, uint32_t config, uint32_t control)
{
	uint32_t divider = config & 0x3f;
	uint64_t feedback = (uint64_t)((config >> 8) & 0xfff) << 24;

	if (!divider) {
		return 0;
	}
	if (!(control & BIT(24))) {
		feedback += control & 0xffffff;
	}
	return ((feedback * reference / divider) >> 24);
}

static int get_rate(const struct a210_clock_tree *t, unsigned int id, uint32_t *rate,
		    unsigned int depth, int index)
{
	const struct a210_tree_clock *c;
	uint32_t source = 0, divisor, value;
	int p, ret;

	if (id >= t->count || depth > 32) {
		return -EINVAL;
	}
	c = &t->clocks[id];
	if (index >= 0 &&
	    (c->kind == A210_POSTDIV || (!(c->flags & PROPAGATE) && c->kind != A210_PLL))) {
		if (index) {
			return -ERANGE;
		}
		index = -1;
	}
	if (c->kind == A210_FIXED) {
		*rate = id ? t->rtc : t->oscillator;
		return index > 0 ? -ERANGE : 0;
	}
	if (c->kind == A210_AON) {
		value = reg(t, c, 0);
		*rate = t->aon_rates[(value >> 4) & 3] / ((value & 7) + 1);
		return index > 0 ? -ERANGE : 0;
	}
	p = parent(t, c);
	if (p < 0) {
		return p;
	}
	/* Divisors enumerate against the currently selected parent rate. */
	ret = get_rate(t, p, &source, depth + 1,
		       c->kind == A210_DIV || c->kind == A210_PLL ? -1 : index);
	if (ret) {
		return ret;
	}
	switch (c->kind) {
	case A210_FACTOR:
		*rate = (uint64_t)source * c->arg0 / c->arg1;
		break;
	case A210_PLL:
		if (index > 0) {
			return -ERANGE;
		}
		*rate = index == 0 ? t->plls[c->arg0].nominal_rate
				   : pll_rate(source, reg(t, c, 0), reg(t, c, 4));
		break;
	case A210_POSTDIV:
		if (reg(t, c, 4) & PLL_BYPASS) {
			*rate = t->oscillator;
			break;
		}
		value = reg(t, c, 0);
		divisor = ((value >> 20) & 7) * ((value >> 24) & 7);
		*rate = divisor ? source / divisor : 0;
		break;
	case A210_DIV:
		if (index >= 0) {
			if ((unsigned int)index > c->arg1 - c->arg0) {
				return -ERANGE;
			}
			divisor = c->arg1 - index;
		} else {
			divisor = (reg(t, c, 0) >> c->shift) & field_mask(c);
		}
		divisor += !!(c->flags & CDE_DIVISOR);
		*rate = divisor ? DIV_ROUND_UP((uint64_t)source, divisor) : 0;
		break;
	default:
		*rate = source;
		break;
	}
	return *rate ? 0 : -EIO;
}

int a210_tree_rate(const struct a210_clock_tree *t, unsigned int id, uint32_t *rate)
{
	return get_rate(t, id, rate, 0, -1);
}

int a210_tree_describe(const struct a210_clock_tree *t, unsigned int id, unsigned int index,
		       uint32_t *rate)
{
	return get_rate(t, id, rate, 0, index);
}

int a210_tree_rate_count(const struct a210_clock_tree *t, unsigned int id)
{
	for (unsigned int depth = 0; depth < 32 && id < t->count; depth++) {
		const struct a210_tree_clock *c = &t->clocks[id];
		int p;

		if (c->kind == A210_DIV) {
			return c->arg1 - c->arg0 + 1;
		}
		if (c->kind == A210_PLL || c->kind == A210_POSTDIV || c->kind == A210_FIXED ||
		    c->kind == A210_AON || !(c->flags & PROPAGATE)) {
			return 1;
		}
		p = parent(t, c);
		if (p < 0) {
			return p;
		}
		id = p;
	}
	return -EINVAL;
}

static int update(const struct a210_clock_tree *t, const struct a210_tree_clock *c,
		  unsigned int offset, uint32_t mask, uint32_t value)
{
	return t->update(t->context, c->bank, c->offset + offset, mask, value);
}

static int wait_pll(const struct a210_clock_tree *t, const struct a210_tree_clock *c)
{
	uint32_t start = t->time_us(t->context);

	do {
		if (reg(t, c, 0x10) & BIT(0)) {
			t->delay_us(t->context, 30);
			return 0;
		}
	} while ((uint32_t)(t->time_us(t->context) - start) < 10000);
	return -ETIMEDOUT;
}

static int set_pll(struct a210_clock_tree *t, unsigned int id)
{
	const struct a210_tree_clock *c = &t->clocks[id];
	const struct a210_tree_pll *p = &t->plls[c->arg0];
	uint32_t old0 = reg(t, c, 0), old1 = reg(t, c, 4);
	int ret;

	if ((old0 & PLL_CONFIG_MASK) == p->config &&
	    (old1 & GENMASK(25, 0)) == (p->fraction ? p->fraction : PLL_INTEGER)) {
		return 0;
	}
	if (t->users[id]) {
		return -EBUSY;
	}
	ret = update(t, c, 4, PLL_RESET, PLL_RESET);
	if (!ret) {
		ret = update(t, c, 0, PLL_CONFIG_MASK, p->config);
	}
	if (!ret) {
		ret = update(t, c, 4, GENMASK(25, 0), p->fraction ? p->fraction : PLL_INTEGER);
	}
	if (!ret) {
		t->delay_us(t->context, 3);
		ret = update(t, c, 4, PLL_RESET, 0);
	}
	if (!ret) {
		ret = wait_pll(t, c);
	}
	if (ret) {
		(void)update(t, c, 4, PLL_RESET, PLL_RESET);
		(void)update(t, c, 0, PLL_CONFIG_MASK, old0 & PLL_CONFIG_MASK);
		(void)update(t, c, 4, GENMASK(25, 0) | PLL_RESET,
			     old1 & (GENMASK(25, 0) | PLL_RESET));
		if ((reg(t, c, 0) & PLL_CONFIG_MASK) != (old0 & PLL_CONFIG_MASK) ||
		    (reg(t, c, 4) & (GENMASK(25, 0) | PLL_RESET)) !=
			    (old1 & (GENMASK(25, 0) | PLL_RESET)) ||
		    (!(old1 & PLL_RESET) && wait_pll(t, c))) {
			t->faulted = true;
		}
		return ret;
	}
	return update(t, c, 4, PLL_RESET, old1 & PLL_RESET);
}

static int set_rate(struct a210_clock_tree *t, unsigned int id, uint32_t rate, unsigned int depth)
{
	const struct a210_tree_clock *c;
	uint32_t current, source;
	int p, ret;

	if (id >= t->count || depth > 32 || !rate) {
		return -EINVAL;
	}
	if (t->faulted) {
		return -EIO;
	}
	c = &t->clocks[id];
	ret = a210_tree_rate(t, id, &current);
	if (ret && ret != -EIO) {
		return ret;
	}
	if (!ret && current == rate) {
		return 0;
	}
	if (c->kind == A210_PLL) {
		ret = a210_tree_describe(t, id, 0, &current);
		return ret ? ret : (current == rate ? set_pll(t, id) : -ERANGE);
	}
	p = parent(t, c);
	if (p < 0) {
		return p;
	}
	if (c->kind == A210_DIV) {
		uint32_t value;
		uint32_t old = reg(t, c, 0);
		uint32_t mask = field_mask(c) << c->shift;

		ret = a210_tree_rate(t, p, &source);
		if (ret) {
			return ret;
		}

		for (value = c->arg0; value <= c->arg1; value++) {
			uint32_t divisor = value + !!(c->flags & CDE_DIVISOR);

			if (DIV_ROUND_UP((uint64_t)source, divisor) == rate) {
				break;
			}
		}
		if (value > c->arg1) {
			return -ERANGE;
		}
		if (c->sync != 255) {
			ret = update(t, c, 0, BIT(c->sync), 0);
			if (ret) {
				return ret;
			}
			t->delay_us(t->context, 1);
		}
		ret = update(t, c, 0, field_mask(c) << c->shift, value << c->shift);
		if (!ret && c->sync != 255) {
			t->delay_us(t->context, 1);
			ret = update(t, c, 0, BIT(c->sync), BIT(c->sync));
		}
		if (ret) {
			if (c->sync != 255) {
				(void)update(t, c, 0, BIT(c->sync), 0);
			}
			(void)update(t, c, 0, mask, old & mask);
			if (c->sync != 255) {
				t->delay_us(t->context, 1);
				mask |= BIT(c->sync);
				(void)update(t, c, 0, BIT(c->sync), old & BIT(c->sync));
			}
			t->faulted = (reg(t, c, 0) & mask) != (old & mask);
		}
		return ret;
	}
	if (!(c->flags & PROPAGATE)) {
		return -EACCES;
	}
	if (c->kind == A210_FACTOR) {
		int count = a210_tree_rate_count(t, p);
		bool found = false;

		for (int i = 0; i < count; i++) {
			ret = a210_tree_describe(t, p, i, &source);
			if (ret) {
				return ret;
			}
			if ((uint64_t)source * c->arg0 / c->arg1 == rate) {
				rate = source;
				found = true;
				break;
			}
		}
		if (!found) {
			return count < 0 ? count : -ERANGE;
		}
	} else if (c->kind == A210_POSTDIV) {
		return -EACCES;
	}
	return set_rate(t, p, rate, depth + 1);
}

int a210_tree_set_rate(struct a210_clock_tree *t, unsigned int id, uint32_t rate)
{
	return set_rate(t, id, rate, 0);
}

int a210_tree_set_parent(struct a210_clock_tree *t, unsigned int id, unsigned int requested)
{
	const struct a210_tree_clock *c;
	unsigned int select;
	int old, ret;

	if (id >= t->count || requested >= t->count || t->faulted) {
		return -EINVAL;
	}
	c = &t->clocks[id];
	if (c->kind != A210_MUX) {
		return -ENOTSUP;
	}
	for (select = 0; select < c->arg1; select++) {
		if (t->parents[c->arg0 + select] == requested) {
			break;
		}
	}
	if (select == c->arg1) {
		return -EINVAL;
	}
	old = parent(t, c);
	if (old < 0 || old == requested) {
		return old < 0 ? old : 0;
	}
	if (t->users[id]) {
		ret = a210_tree_enable(t, requested);
		if (ret) {
			return ret;
		}
	}
	ret = update(t, c, 0, field_mask(c) << c->shift, select << c->shift);
	if (ret) {
		if (t->users[id]) {
			(void)a210_tree_disable(t, requested);
		}
		return ret;
	}
	return t->users[id] ? a210_tree_disable(t, old) : 0;
}

int a210_tree_enable(struct a210_clock_tree *t, unsigned int id)
{
	const struct a210_tree_clock *c;
	int p, ret;

	if (id >= t->count || t->users[id] == UINT16_MAX) {
		return -EINVAL;
	}
	if (t->faulted) {
		return -EIO;
	}
	if (t->users[id]++) {
		return 0;
	}
	c = &t->clocks[id];
	if (c->kind == A210_FIXED || c->kind == A210_AON) {
		return 0;
	}
	p = parent(t, c);
	ret = p < 0 ? p : a210_tree_enable(t, p);
	if (ret) {
		t->users[id]--;
		return ret;
	}
	if (c->kind == A210_GATE) {
		ret = update(t, c, 0, BIT(c->shift), BIT(c->shift));
	} else if (c->kind == A210_PLL) {
		uint32_t old = reg(t, c, 4);

		if (old & PLL_RESET) {
			t->delay_us(t->context, 3);
			ret = update(t, c, 4, PLL_RESET, 0);
			if (!ret) {
				ret = wait_pll(t, c);
			}
			if (ret) {
				(void)update(t, c, 4, PLL_RESET, old & PLL_RESET);
			}
		}
	}
	if (ret) {
		(void)a210_tree_disable(t, p);
		t->users[id]--;
	}
	return ret;
}

int a210_tree_disable(struct a210_clock_tree *t, unsigned int id)
{
	const struct a210_tree_clock *c;
	int p, ret = 0;

	if (id >= t->count || !t->users[id]) {
		return -EINVAL;
	}
	if (t->faulted) {
		return -EIO;
	}
	if (t->users[id] > 1) {
		t->users[id]--;
		return 0;
	}
	c = &t->clocks[id];
	if (c->kind == A210_GATE) {
		ret = update(t, c, 0, BIT(c->shift), 0);
	} else if (c->kind == A210_PLL) {
		ret = update(t, c, 4, PLL_RESET, PLL_RESET);
	}
	if (ret) {
		return ret;
	}
	if (c->kind == A210_FIXED || c->kind == A210_AON) {
		t->users[id]--;
		return 0;
	}
	p = parent(t, c);
	ret = p < 0 ? p : a210_tree_disable(t, p);
	if (ret) {
		if (c->kind == A210_GATE) {
			t->faulted |= update(t, c, 0, BIT(c->shift), BIT(c->shift)) != 0;
		} else if (c->kind == A210_PLL) {
			t->faulted |= update(t, c, 4, PLL_RESET, 0) != 0;
		}
		return ret;
	}
	t->users[id]--;
	return 0;
}

int a210_tree_state(const struct a210_clock_tree *t, unsigned int id, bool *enabled)
{
	for (unsigned int depth = 0; depth < 32 && id < t->count; depth++) {
		const struct a210_tree_clock *c = &t->clocks[id];
		int p;

		*enabled = true;
		if (c->kind == A210_FIXED || c->kind == A210_AON) {
			return 0;
		}
		if ((c->kind == A210_GATE && !(reg(t, c, 0) & BIT(c->shift))) ||
		    (c->kind == A210_PLL && (reg(t, c, 4) & PLL_RESET))) {
			*enabled = false;
			return 0;
		}
		p = parent(t, c);
		if (p < 0) {
			return p;
		}
		id = p;
	}
	return -EINVAL;
}
