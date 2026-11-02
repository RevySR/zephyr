/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_A210_CLOCK_TREE_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_A210_CLOCK_TREE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

enum a210_clock_kind {
	A210_FIXED,
	A210_FACTOR,
	A210_PLL,
	A210_POSTDIV,
	A210_DIV,
	A210_MUX,
	A210_GATE,
	A210_AON,
};

/* a210_tree_clock.flags */
#define A210_CLOCK_PROPAGATE   BIT(0) /* Pass rate requests on to the parent. */
#define A210_CLOCK_CDE_DIVISOR BIT(1) /* The divider field holds the divisor minus one. */

/* a210_tree_clock.sync of a divider without a sync enable bit */
#define A210_CLOCK_NO_SYNC 0xff

/* Settling time around a divider sync enable toggle. */
#define A210_CLOCK_SYNC_DELAY_US 1

/* a210_tree_clock.arg0 of an A210_FIXED clock */
enum a210_fixed_source {
	A210_FIXED_OSCILLATOR,
	A210_FIXED_RTC,
};

/* PLL registers, relative to a210_tree_clock.offset. */
#define A210_PLL_CFG0           0x00
#define A210_PLL_REFDIV         GENMASK(5, 0)
#define A210_PLL_FBDIV          GENMASK(19, 8)
#define A210_PLL_POSTDIV1       GENMASK(22, 20)
#define A210_PLL_POSTDIV2       GENMASK(26, 24)
#define A210_PLL_CFG1           0x04
#define A210_PLL_FRAC_BITS      24
#define A210_PLL_FRAC           GENMASK(A210_PLL_FRAC_BITS - 1, 0)
#define A210_PLL_INTEGER_MODE   BIT(24)
#define A210_PLL_DAC_POWER_DOWN BIT(25)
#define A210_PLL_RESET          BIT(30)
#define A210_PLL_BYPASS         BIT(31)
#define A210_PLL_STATUS         0x10
#define A210_PLL_LOCK           BIT(0)

#define A210_PLL_INTEGER (A210_PLL_INTEGER_MODE | A210_PLL_DAC_POWER_DOWN)
#define A210_PLL_CONFIG(refdiv, fbdiv, postdiv1, postdiv2)                                         \
	(FIELD_PREP(A210_PLL_REFDIV, refdiv) | FIELD_PREP(A210_PLL_FBDIV, fbdiv) |                 \
	 FIELD_PREP(A210_PLL_POSTDIV1, postdiv1) | FIELD_PREP(A210_PLL_POSTDIV2, postdiv2))

/* Source clocks selectable for the AON system clock. */
#define A210_AON_SOURCES 4

/* arg0/arg1 describe a factor, divisor range, parent list, or PLL profile. */
struct a210_tree_clock {
	uint8_t kind;
	uint8_t bank;
	uint16_t offset;
	uint16_t parent;
	uint16_t arg0;
	uint16_t arg1;
	uint8_t shift;
	uint8_t width;
	uint8_t sync;
	uint8_t flags;
};

struct a210_tree_pll {
	uint32_t config;
	uint32_t fraction;
	uint32_t nominal_rate;
};

struct a210_clock_word {
	uint8_t bank;
	uint16_t offset;
	uint32_t mask;
	uint32_t sync;
};

struct a210_clock_tree {
	const struct a210_tree_clock *clocks;
	const struct a210_tree_pll *plls;
	const uint16_t *parents;
	uint16_t count;
	uint16_t *users;
	bool faulted;
	uint32_t oscillator;
	uint32_t rtc;
	uint32_t aon_rates[A210_AON_SOURCES];
	void *context;
	uint32_t (*read)(void *context, unsigned int bank, unsigned int offset);
	int (*update)(void *context, unsigned int bank, unsigned int offset, uint32_t mask,
		      uint32_t value);
	void (*delay_us)(void *context, uint32_t us);
	uint32_t (*time_us)(void *context);
};

int a210_tree_rate(const struct a210_clock_tree *t, unsigned int id, uint32_t *rate);
int a210_tree_describe(const struct a210_clock_tree *t, unsigned int id, unsigned int index,
		       uint32_t *rate);
int a210_tree_rate_count(const struct a210_clock_tree *t, unsigned int id);
int a210_tree_set_rate(struct a210_clock_tree *t, unsigned int id, uint32_t rate);
int a210_tree_set_parent(struct a210_clock_tree *t, unsigned int id, unsigned int parent);
int a210_tree_enable(struct a210_clock_tree *t, unsigned int id);
int a210_tree_disable(struct a210_clock_tree *t, unsigned int id);
int a210_tree_state(const struct a210_clock_tree *t, unsigned int id, bool *enabled);

#endif
