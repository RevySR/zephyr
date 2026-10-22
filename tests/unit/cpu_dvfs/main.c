/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>
#include "cpu_dvfs.h"

struct hardware {
	uint32_t hz;
	int32_t uv;
	int clock_error, voltage_error;
	bool wrong_clock_readback;
	char trace[16];
	size_t steps;
};

static int get_rate(void *ctx, uint32_t *hz)
{
	*hz = ((struct hardware *)ctx)->hz;
	return 0;
}
static int get_voltage(void *ctx, unsigned int supply, int32_t *uv)
{
	ARG_UNUSED(supply);
	*uv = ((struct hardware *)ctx)->uv;
	return 0;
}
static int set_rate(void *ctx, uint32_t hz)
{
	struct hardware *h = ctx;

	h->trace[h->steps++] = 'C';
	zassert_true(hz <= 1000000000 || h->uv >= 1000000);
	if (!h->clock_error && !h->wrong_clock_readback) {
		h->hz = hz;
	}
	return h->clock_error;
}
static int set_voltage(void *ctx, unsigned int supply, int32_t uv)
{
	struct hardware *h = ctx;

	ARG_UNUSED(supply);
	h->trace[h->steps++] = 'V';
	zassert_true(uv >= 1000000 || h->hz <= 1000000000);
	if (!h->voltage_error) {
		h->uv = uv;
	}
	return h->voltage_error;
}

static const struct cpu_dvfs_opp opps[] = {{1000000000, 800000}, {1800000000, 1000000}};
static const struct cpu_dvfs_ops ops = {get_rate, set_rate, get_voltage, set_voltage};

ZTEST(cpu_dvfs, test_transition_order_and_readback)
{
	struct hardware h = {.hz = 1000000000, .uv = 800000};
	struct cpu_dvfs d = {opps, ARRAY_SIZE(opps), &ops, &h, false, 1};

	zassert_equal(cpu_dvfs_set_rate(&d, 1800000000), 0);
	zassert_mem_equal(h.trace, "VC", 2);
	zassert_equal(h.hz, 1800000000);
	zassert_equal(h.uv, 1000000);
	h.steps = 0;
	zassert_equal(cpu_dvfs_set_rate(&d, 1000000000), 0);
	zassert_mem_equal(h.trace, "CV", 2);
	zassert_equal(h.hz, 1000000000);
	zassert_equal(h.uv, 800000);
	h.steps = 0;
	zassert_equal(cpu_dvfs_set_rate(&d, 1000000000), 0);
	zassert_equal(h.steps, 0);
	zassert_equal(cpu_dvfs_set_rate(&d, 1000000001), -ERANGE);
	zassert_equal(h.steps, 0);
}

ZTEST(cpu_dvfs, test_raise_voltage_failure_leaves_clock)
{
	struct hardware h = {.hz = 1000000000, .uv = 800000, .voltage_error = -ENXIO};
	struct cpu_dvfs d = {opps, ARRAY_SIZE(opps), &ops, &h, false, 1};

	zassert_equal(cpu_dvfs_set_rate(&d, 1800000000), -ENXIO);
	zassert_equal(h.steps, 1);
	zassert_equal(h.trace[0], 'V');
	zassert_equal(h.hz, 1000000000);
}

ZTEST(cpu_dvfs, test_failed_clock_never_lowers_voltage)
{
	struct hardware h = {.hz = 1800000000, .uv = 1000000, .clock_error = -ETIMEDOUT};
	struct cpu_dvfs d = {opps, ARRAY_SIZE(opps), &ops, &h, false, 1};

	zassert_equal(cpu_dvfs_set_rate(&d, 1000000000), -ETIMEDOUT);
	zassert_equal(h.steps, 1);
	zassert_equal(h.trace[0], 'C');
	zassert_equal(h.uv, 1000000);
	h.clock_error = 0;
	zassert_equal(cpu_dvfs_set_rate(&d, 1000000000), 0);
	zassert_equal(h.uv, 800000);
}

ZTEST(cpu_dvfs, test_uncertain_supply_keeps_lower_clock)
{
	struct hardware h = {.hz = 1800000000, .uv = 1000000, .voltage_error = -ENXIO};
	struct cpu_dvfs d = {opps, ARRAY_SIZE(opps), &ops, &h, false, 1};

	zassert_equal(cpu_dvfs_set_rate(&d, 1000000000), -ENXIO);
	zassert_mem_equal(h.trace, "CV", 2);
	zassert_equal(h.hz, 1000000000);
	zassert_true(d.faulted);
	zassert_equal(cpu_dvfs_set_rate(&d, 1800000000), -EIO);
	zassert_equal(h.hz, 1000000000);
}

ZTEST(cpu_dvfs, test_clock_readback_mismatch_prevents_voltage_drop)
{
	struct hardware h = {.hz = 1800000000, .uv = 1000000, .wrong_clock_readback = true};
	struct cpu_dvfs d = {opps, ARRAY_SIZE(opps), &ops, &h, false, 1};

	zassert_equal(cpu_dvfs_set_rate(&d, 1000000000), -EIO);
	zassert_equal(h.steps, 1);
	zassert_equal(h.uv, 1000000);
	zassert_true(d.faulted);
}
struct pair {
	uint32_t hz;
	int32_t uv[2];
	char trace[16];
	unsigned int steps;
};
static int pair_get_rate(void *ctx, uint32_t *hz)
{
	*hz = ((struct pair *)ctx)->hz;
	return 0;
}
static int pair_set_rate(void *ctx, uint32_t hz)
{
	struct pair *p = ctx;

	p->trace[p->steps++] = 'C';
	if (hz > 1200000000) {
		zassert_equal(p->uv[0], 1000000);
		zassert_equal(p->uv[1], 1000000);
	}
	p->hz = hz;
	return 0;
}
static int pair_get_voltage(void *ctx, unsigned int supply, int32_t *uv)
{
	*uv = ((struct pair *)ctx)->uv[supply];
	return 0;
}
static int pair_set_voltage(void *ctx, unsigned int supply, int32_t uv)
{
	struct pair *p = ctx;

	if (uv < 1000000) {
		zassert_equal(p->hz, 1200000000);
	}
	p->trace[p->steps++] = supply ? 'M' : 'V';
	p->uv[supply] = uv;
	zassert_true(p->uv[1] >= p->uv[0]);
	zassert_true(p->uv[1] - p->uv[0] <= 300000);
	return 0;
}
ZTEST(cpu_dvfs, test_dual_supply_order)
{
	const struct cpu_dvfs_opp table[] = {
		{1200000000, 700000, 800000},
		{1896000000, 1000000, 1000000},
	};
	const struct cpu_dvfs_ops pair_ops = {
		pair_get_rate,
		pair_set_rate,
		pair_get_voltage,
		pair_set_voltage,
	};
	struct pair p = {.hz = 1200000000, .uv = {700000, 800000}};
	struct cpu_dvfs d = {table, ARRAY_SIZE(table), &pair_ops, &p, false, 2};

	zassert_ok(cpu_dvfs_set_rate(&d, 1896000000));
	zassert_mem_equal(p.trace, "MVC", 3);
	p.steps = 0;
	zassert_ok(cpu_dvfs_set_rate(&d, 1200000000));
	zassert_mem_equal(p.trace, "CVM", 3);
	zassert_equal(p.uv[0], 700000);
	zassert_equal(p.uv[1], 800000);
}
ZTEST_SUITE(cpu_dvfs, NULL, NULL, NULL, NULL, NULL);
