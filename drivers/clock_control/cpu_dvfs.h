/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_CPU_DVFS_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_CPU_DVFS_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cpu_dvfs_opp {
	uint32_t hz;
	int32_t uv;
	int32_t sram_uv;
};

struct cpu_dvfs_ops {
	int (*get_rate)(void *context, uint32_t *hz);
	int (*set_rate)(void *context, uint32_t hz);
	int (*get_voltage)(void *context, unsigned int supply, int32_t *uv);
	int (*set_voltage)(void *context, unsigned int supply, int32_t uv);
};

struct cpu_dvfs {
	const struct cpu_dvfs_opp *opps;
	size_t count;
	const struct cpu_dvfs_ops *ops;
	void *context;
	bool faulted;
	unsigned int supply_count;
};

int cpu_dvfs_set_rate(struct cpu_dvfs *domain, uint32_t hz);
#endif
