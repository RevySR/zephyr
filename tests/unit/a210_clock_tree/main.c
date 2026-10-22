/* SPDX-License-Identifier: Apache-2.0 */
#include "a210_clock_tree.h"
#include "a210_clock_data.h"
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static uint32_t registers[13][1024];
static uint16_t users[ARRAY_SIZE(clock_descs)];
static unsigned int writes;
static uint32_t time_us;
static bool reject;

static uint32_t read(void *context, unsigned int bank, unsigned int offset)
{
	ARG_UNUSED(context);
	zassert_true(bank < ARRAY_SIZE(registers) && offset / 4 < 1024);
	return registers[bank][offset / 4];
}

static int update(void *context, unsigned int bank, unsigned int offset, uint32_t mask,
		  uint32_t value)
{
	ARG_UNUSED(context);
	zassert_equal(value & ~mask, 0);
	writes++;
	if (reject) {
		return -EIO;
	}
	registers[bank][offset / 4] = (registers[bank][offset / 4] & ~mask) | value;
	return 0;
}

static void delay(void *context, uint32_t us)
{
	ARG_UNUSED(context);
	time_us += us;
}

static uint32_t time(void *context)
{
	ARG_UNUSED(context);
	return time_us++;
}

static struct a210_clock_tree tree = {
	.clocks = clock_descs,
	.plls = clock_plls,
	.parents = clock_parents,
	.count = ARRAY_SIZE(clock_descs),
	.users = users,
	.oscillator = 24000000,
	.rtc = 32768,
	.aon_rates = {110000000, 24000000, 26000000, 32768},
	.read = read,
	.update = update,
	.delay_us = delay,
	.time_us = time,
};

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(registers, 0, sizeof(registers));
	memset(users, 0, sizeof(users));
	writes = time_us = 0;
	reject = false;
	tree.faulted = false;
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_descs); i++) {
		const struct a210_tree_clock *c = &clock_descs[i];

		if (c->kind == A210_PLL) {
			registers[c->bank][c->offset / 4] = clock_plls[c->arg0].config;
			registers[c->bank][c->offset / 4 + 1] =
				clock_plls[c->arg0].fraction ? clock_plls[c->arg0].fraction
							     : BIT(24) | BIT(25);
			registers[c->bank][c->offset / 4 + 4] = 1;
		} else if (c->kind == A210_DIV) {
			registers[c->bank][c->offset / 4] |= c->arg0 << c->shift;
		}
	}
}

ZTEST(a210_clock_tree, test_complete_rate_enumeration)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_map); i++) {
		int count = a210_tree_rate_count(&tree, clock_map[i]);
		uint32_t previous = 0, rate;

		zassert_true(count > 0, "clock %u", i);
		zassert_ok(a210_tree_rate(&tree, clock_map[i], &rate), "clock %u", i);
		for (int j = 0; j < count; j++) {
			zassert_ok(a210_tree_describe(&tree, clock_map[i], j, &rate),
				   "clock %u rate %u", i, j);
			zassert_true(rate > previous, "clock %u rate %u", i, j);
			previous = rate;
		}
		zassert_equal(a210_tree_describe(&tree, clock_map[i], count, &rate), -ERANGE);
	}
	zassert_equal(writes, 0);
}

ZTEST(a210_clock_tree, test_shared_parent_votes_and_failed_enable)
{
	bool enabled;
	unsigned int uart0 = clock_map[0], uart1 = clock_map[1];
	unsigned int shared = clock_descs[uart0].parent;

	zassert_ok(a210_tree_enable(&tree, uart0));
	zassert_ok(a210_tree_enable(&tree, uart1));
	zassert_equal(users[shared], 2);
	zassert_ok(a210_tree_disable(&tree, uart0));
	zassert_equal(users[shared], 1);
	zassert_ok(a210_tree_state(&tree, uart0, &enabled));
	zassert_false(enabled);
	zassert_ok(a210_tree_state(&tree, uart1, &enabled));
	zassert_true(enabled);
	reject = true;
	zassert_equal(a210_tree_enable(&tree, uart0), -EIO);
	zassert_equal(users[uart0], 0);
	zassert_equal(users[shared], 1);
}

ZTEST(a210_clock_tree, test_dividers_round_and_preserve_neighbors)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_descs); i++) {
		const struct a210_tree_clock *c = &clock_descs[i];
		uint32_t rate, actual, original;
		uint32_t mask;

		if (c->kind != A210_DIV || c->arg0 == c->arg1) {
			continue;
		}
		original = registers[c->bank][c->offset / 4];
		mask = (BIT(c->width) - 1) << c->shift;
		if (c->sync != 255) {
			mask |= BIT(c->sync);
		}
		zassert_ok(a210_tree_describe(&tree, i, 0, &rate));
		zassert_ok(a210_tree_set_rate(&tree, i, rate));
		zassert_ok(a210_tree_rate(&tree, i, &actual));
		zassert_equal(actual, rate);
		zassert_equal(registers[c->bank][c->offset / 4] & ~mask, original & ~mask);
	}
}

ZTEST(a210_clock_tree, test_critical_sources_remain_referenced)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_critical); i++) {
		zassert_ok(a210_tree_enable(&tree, clock_critical[i]));
	}
	zassert_ok(a210_tree_enable(&tree, clock_map[4]));
	zassert_ok(a210_tree_disable(&tree, clock_map[4]));
	zassert_true(users[clock_map[4]] > 0);
}

ZTEST(a210_clock_tree, test_pll_failure_is_bounded_and_preserves_configuration)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_descs); i++) {
		const struct a210_tree_clock *c = &clock_descs[i];
		uint32_t original, requested;

		if (c->kind != A210_PLL) {
			continue;
		}
		original = registers[c->bank][c->offset / 4] ^ BIT(8);
		registers[c->bank][c->offset / 4] = original;
		registers[c->bank][c->offset / 4 + 4] = 0;
		zassert_ok(a210_tree_describe(&tree, i, 0, &requested));
		zassert_equal(a210_tree_set_rate(&tree, i, requested), -ETIMEDOUT);
		zassert_equal(registers[c->bank][c->offset / 4], original);
		zassert_true(time_us >= 20000 && time_us < 20100);
		zassert_true(tree.faulted);
		break;
	}
}

ZTEST(a210_clock_tree, test_program_divider_after_power_loss)
{
	bool tested = false;

	for (unsigned int i = 0; i < ARRAY_SIZE(clock_descs); i++) {
		const struct a210_tree_clock *gate = &clock_descs[i];
		const struct a210_tree_clock *div;
		uint32_t rate, actual;

		if (gate->kind != A210_GATE) {
			continue;
		}
		div = &clock_descs[gate->parent];
		if (div->kind != A210_DIV || (div->flags & BIT(1))) {
			continue;
		}
		registers[div->bank][div->offset / 4] &= ~((BIT(div->width) - 1) << div->shift);
		zassert_ok(a210_tree_describe(&tree, i, 0, &rate));
		zassert_ok(a210_tree_set_rate(&tree, i, rate));
		zassert_ok(a210_tree_rate(&tree, i, &actual));
		zassert_equal(actual, rate);
		tested = true;
		break;
	}
	zassert_true(tested);
}

ZTEST(a210_clock_tree, test_inherit_running_pll_without_restarting_it)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(clock_descs); i++) {
		const struct a210_tree_clock *c = &clock_descs[i];

		if (c->kind == A210_PLL) {
			registers[c->bank][c->offset / 4 + 4] = 0;
			zassert_ok(a210_tree_enable(&tree, i));
			zassert_equal(writes, 0);
			break;
		}
	}
}

ZTEST(a210_clock_tree, test_boot_parent_preserves_reference_counts)
{
	bool tested = false;

	for (unsigned int i = 0; i < ARRAY_SIZE(clock_descs); i++) {
		const struct a210_tree_clock *c = &clock_descs[i];
		unsigned int old_parent, new_parent;
		uint32_t expected, actual;

		if (c->kind != A210_MUX || c->arg1 < 2) {
			continue;
		}
		old_parent = clock_parents[c->arg0];
		new_parent = clock_parents[c->arg0 + 1];
		zassert_ok(a210_tree_enable(&tree, i));
		zassert_ok(a210_tree_set_parent(&tree, i, new_parent));
		zassert_equal(users[old_parent], 0);
		zassert_equal(users[new_parent], 1);
		zassert_ok(a210_tree_rate(&tree, new_parent, &expected));
		zassert_ok(a210_tree_rate(&tree, i, &actual));
		zassert_equal(actual, expected);
		tested = true;
		break;
	}
	zassert_true(tested);
}

ZTEST_SUITE(a210_clock_tree, NULL, NULL, before, NULL, NULL);
