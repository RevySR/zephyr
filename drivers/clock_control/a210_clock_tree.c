/* SPDX-License-Identifier: Apache-2.0 */
#include "a210_clock_tree.h"
#include <zephyr/sys/util.h>
#include <errno.h>

/* Parent chains longer than this indicate a loop in the clock table. */
#define MAX_DEPTH 32

/* PLL fields programmed from a rate profile. */
#define PLL_CFG0_FIELDS  (A210_PLL_POSTDIV2 | A210_PLL_POSTDIV1 | A210_PLL_FBDIV | A210_PLL_REFDIV)
#define PLL_CFG1_FIELDS  (A210_PLL_DAC_POWER_DOWN | A210_PLL_INTEGER_MODE | A210_PLL_FRAC)
#define PLL_CFG1_RESTORE (PLL_CFG1_FIELDS | A210_PLL_RESET)

#define PLL_RESET_US        3
#define PLL_LOCK_SETTLE_US  30
#define PLL_LOCK_TIMEOUT_US 10000

/* AON system clock configuration register */
#define AON_CLK_DIVIDER GENMASK(2, 0)
#define AON_CLK_SOURCE  GENMASK(5, 4)

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
	uint32_t divider = FIELD_GET(A210_PLL_REFDIV, config);
	uint64_t feedback = (uint64_t)FIELD_GET(A210_PLL_FBDIV, config) << A210_PLL_FRAC_BITS;

	if (!divider) {
		return 0;
	}
	if (!(control & A210_PLL_INTEGER_MODE)) {
		feedback += FIELD_GET(A210_PLL_FRAC, control);
	}
	return ((feedback * reference / divider) >> A210_PLL_FRAC_BITS);
}

static int get_rate(const struct a210_clock_tree *t, unsigned int id, uint32_t *rate,
		    unsigned int depth, int index)
{
	const struct a210_tree_clock *c;
	uint32_t source = 0, divisor, value;
	int p, ret;

	if (id >= t->count || depth > MAX_DEPTH) {
		return -EINVAL;
	}
	c = &t->clocks[id];
	if (index >= 0 && (c->kind == A210_POSTDIV ||
			   (!(c->flags & A210_CLOCK_PROPAGATE) && c->kind != A210_PLL))) {
		if (index) {
			return -ERANGE;
		}
		index = -1;
	}
	if (c->kind == A210_FIXED) {
		*rate = c->arg0 == A210_FIXED_RTC ? t->rtc : t->oscillator;
		return index > 0 ? -ERANGE : 0;
	}
	if (c->kind == A210_AON) {
		value = reg(t, c, 0);
		*rate = t->aon_rates[FIELD_GET(AON_CLK_SOURCE, value)] /
			(FIELD_GET(AON_CLK_DIVIDER, value) + 1);
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
				   : pll_rate(source, reg(t, c, A210_PLL_CFG0),
					      reg(t, c, A210_PLL_CFG1));
		break;
	case A210_POSTDIV:
		if (reg(t, c, A210_PLL_CFG1) & A210_PLL_BYPASS) {
			*rate = t->oscillator;
			break;
		}
		value = reg(t, c, A210_PLL_CFG0);
		divisor = FIELD_GET(A210_PLL_POSTDIV1, value) * FIELD_GET(A210_PLL_POSTDIV2, value);
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
		divisor += !!(c->flags & A210_CLOCK_CDE_DIVISOR);
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
	for (unsigned int depth = 0; depth < MAX_DEPTH && id < t->count; depth++) {
		const struct a210_tree_clock *c = &t->clocks[id];
		int p;

		if (c->kind == A210_DIV) {
			return c->arg1 - c->arg0 + 1;
		}
		if (c->kind == A210_PLL || c->kind == A210_POSTDIV || c->kind == A210_FIXED ||
		    c->kind == A210_AON || !(c->flags & A210_CLOCK_PROPAGATE)) {
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
		if (reg(t, c, A210_PLL_STATUS) & A210_PLL_LOCK) {
			t->delay_us(t->context, PLL_LOCK_SETTLE_US);
			return 0;
		}
	} while ((uint32_t)(t->time_us(t->context) - start) < PLL_LOCK_TIMEOUT_US);
	return -ETIMEDOUT;
}

static int set_pll(struct a210_clock_tree *t, unsigned int id)
{
	const struct a210_tree_clock *c = &t->clocks[id];
	const struct a210_tree_pll *p = &t->plls[c->arg0];
	uint32_t old0 = reg(t, c, A210_PLL_CFG0), old1 = reg(t, c, A210_PLL_CFG1);
	uint32_t cfg1 = p->fraction ? p->fraction : A210_PLL_INTEGER;
	int ret;

	if ((old0 & PLL_CFG0_FIELDS) == p->config && (old1 & PLL_CFG1_FIELDS) == cfg1) {
		return 0;
	}
	if (t->users[id]) {
		return -EBUSY;
	}
	ret = update(t, c, A210_PLL_CFG1, A210_PLL_RESET, A210_PLL_RESET);
	if (!ret) {
		ret = update(t, c, A210_PLL_CFG0, PLL_CFG0_FIELDS, p->config);
	}
	if (!ret) {
		ret = update(t, c, A210_PLL_CFG1, PLL_CFG1_FIELDS, cfg1);
	}
	if (!ret) {
		t->delay_us(t->context, PLL_RESET_US);
		ret = update(t, c, A210_PLL_CFG1, A210_PLL_RESET, 0);
	}
	if (!ret) {
		ret = wait_pll(t, c);
	}
	if (ret) {
		(void)update(t, c, A210_PLL_CFG1, A210_PLL_RESET, A210_PLL_RESET);
		(void)update(t, c, A210_PLL_CFG0, PLL_CFG0_FIELDS, old0 & PLL_CFG0_FIELDS);
		(void)update(t, c, A210_PLL_CFG1, PLL_CFG1_RESTORE, old1 & PLL_CFG1_RESTORE);
		if ((reg(t, c, A210_PLL_CFG0) & PLL_CFG0_FIELDS) != (old0 & PLL_CFG0_FIELDS) ||
		    (reg(t, c, A210_PLL_CFG1) & PLL_CFG1_RESTORE) != (old1 & PLL_CFG1_RESTORE) ||
		    (!(old1 & A210_PLL_RESET) && wait_pll(t, c))) {
			t->faulted = true;
		}
		return ret;
	}
	return update(t, c, A210_PLL_CFG1, A210_PLL_RESET, old1 & A210_PLL_RESET);
}

static int set_rate(struct a210_clock_tree *t, unsigned int id, uint32_t rate, unsigned int depth)
{
	const struct a210_tree_clock *c;
	uint32_t current, source;
	int p, ret;

	if (id >= t->count || depth > MAX_DEPTH || !rate) {
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
			uint32_t divisor = value + !!(c->flags & A210_CLOCK_CDE_DIVISOR);

			if (DIV_ROUND_UP((uint64_t)source, divisor) == rate) {
				break;
			}
		}
		if (value > c->arg1) {
			return -ERANGE;
		}
		if (c->sync != A210_CLOCK_NO_SYNC) {
			ret = update(t, c, 0, BIT(c->sync), 0);
			if (ret) {
				return ret;
			}
			t->delay_us(t->context, A210_CLOCK_SYNC_DELAY_US);
		}
		ret = update(t, c, 0, field_mask(c) << c->shift, value << c->shift);
		if (!ret && c->sync != A210_CLOCK_NO_SYNC) {
			t->delay_us(t->context, A210_CLOCK_SYNC_DELAY_US);
			ret = update(t, c, 0, BIT(c->sync), BIT(c->sync));
		}
		if (ret) {
			if (c->sync != A210_CLOCK_NO_SYNC) {
				(void)update(t, c, 0, BIT(c->sync), 0);
			}
			(void)update(t, c, 0, mask, old & mask);
			if (c->sync != A210_CLOCK_NO_SYNC) {
				t->delay_us(t->context, A210_CLOCK_SYNC_DELAY_US);
				mask |= BIT(c->sync);
				(void)update(t, c, 0, BIT(c->sync), old & BIT(c->sync));
			}
			t->faulted = (reg(t, c, 0) & mask) != (old & mask);
		}
		return ret;
	}
	if (!(c->flags & A210_CLOCK_PROPAGATE)) {
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
		uint32_t old = reg(t, c, A210_PLL_CFG1);

		if (old & A210_PLL_RESET) {
			t->delay_us(t->context, PLL_RESET_US);
			ret = update(t, c, A210_PLL_CFG1, A210_PLL_RESET, 0);
			if (!ret) {
				ret = wait_pll(t, c);
			}
			if (ret) {
				(void)update(t, c, A210_PLL_CFG1, A210_PLL_RESET,
					     old & A210_PLL_RESET);
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
		ret = update(t, c, A210_PLL_CFG1, A210_PLL_RESET, A210_PLL_RESET);
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
			t->faulted |= update(t, c, A210_PLL_CFG1, A210_PLL_RESET, 0) != 0;
		}
		return ret;
	}
	t->users[id]--;
	return 0;
}

int a210_tree_state(const struct a210_clock_tree *t, unsigned int id, bool *enabled)
{
	for (unsigned int depth = 0; depth < MAX_DEPTH && id < t->count; depth++) {
		const struct a210_tree_clock *c = &t->clocks[id];
		int p;

		*enabled = true;
		if (c->kind == A210_FIXED || c->kind == A210_AON) {
			return 0;
		}
		if ((c->kind == A210_GATE && !(reg(t, c, 0) & BIT(c->shift))) ||
		    (c->kind == A210_PLL && (reg(t, c, A210_PLL_CFG1) & A210_PLL_RESET))) {
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
