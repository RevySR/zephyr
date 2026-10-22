/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_A210_CLOCK_TREE_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_A210_CLOCK_TREE_H_

#include <stdbool.h>
#include <stdint.h>

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
	uint32_t aon_rates[4];
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
