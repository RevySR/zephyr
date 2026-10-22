/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_XUANTIE_CPU_PLL_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_XUANTIE_CPU_PLL_H_
#include <stdbool.h>
#include <stdint.h>

struct xuantie_cpu_clock {
	uintptr_t pll, mux, backup, gmac, ccu;
	uintptr_t fabric, video;
	bool th1520, faulted;
	uint32_t timer_hz;
	void *context;
	uint32_t (*read32)(void *context, uintptr_t address);
	void (*write32)(void *context, uint32_t value, uintptr_t address);
	uint32_t (*ticks)(void *context);
	void (*delay_us)(void *context, uint32_t us);
};
int xuantie_cpu_clock_get_rate(struct xuantie_cpu_clock *clock, uint32_t *hz);
int xuantie_cpu_clock_set_rate(struct xuantie_cpu_clock *clock, uint32_t hz);
int xuantie_cpu_clock_prepare(struct xuantie_cpu_clock *clock);
#endif
