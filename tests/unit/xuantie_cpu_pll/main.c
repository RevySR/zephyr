/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>
#include "xuantie_cpu_pll.h"

#define CFG(ref, fb, p1, p2) ((ref) | ((fb) << 8) | ((p1) << 20) | ((p2) << 24))
struct model {
	uint32_t regs[1024], time, original;
	bool th1520, fail_new, fail_restore;
	unsigned int writes;
};

static uint32_t read32(void *ctx, uintptr_t reg)
{
	struct model *m = ctx;

	zassert_true(reg < sizeof(m->regs) && !(reg & 3));
	return m->regs[reg / 4];
}
static void write32(void *ctx, uint32_t value, uintptr_t reg)
{
	struct model *m = ctx;
	uint32_t reset = m->th1520 ? BIT(29) : BIT(30);

	zassert_true(reg < sizeof(m->regs) && !(reg & 3));
	m->writes++;
	if (reg == 4 || (m->th1520 && reg == 0x14)) {
		unsigned int pll = reg == 4 ? 0 : 1;
		unsigned int select = m->regs[0x200 / 4] & 1;
		unsigned int status = m->th1520 ? 0x80 : 0x10;
		unsigned int bit = m->th1520 ? BIT(pll ? 4 : 1) : 1;
		bool old = m->regs[(reg - 4) / 4] == m->original;

		/* Resetting the clock currently feeding CPUs is never legal. */
		if (value & reset) {
			zassert_true(m->th1520 ? select != pll : select == 1);
			m->regs[status / 4] &= ~bit;
		} else if (!(old ? m->fail_restore : m->fail_new)) {
			m->regs[status / 4] |= bit;
		}
	}
	m->regs[reg / 4] = value;
}
static uint32_t ticks(void *ctx)
{
	return ++((struct model *)ctx)->time;
}
static void delay_us(void *ctx, uint32_t us)
{
	((struct model *)ctx)->time += us;
}

static struct xuantie_cpu_clock init(struct model *m, bool th1520)
{
	memset(m, 0, sizeof(*m));
	m->th1520 = th1520;
	m->original = th1520 ? CFG(1, 125, 2, 1) : CFG(1, 79, 1, 1);
	m->regs[0] = m->original;
	m->regs[1] = BIT(24) | BIT(25);
	m->regs[0x10 / 4] = th1520 ? CFG(1, 125, 2, 1) : 1;
	m->regs[0x14 / 4] = BIT(24) | BIT(25);
	m->regs[0x80 / 4] = BIT(1) | BIT(4);
	m->regs[0x100 / 4] = CFG(1, 125, 3, 1);
	m->regs[0x104 / 4] = BIT(24) | BIT(25);
	m->regs[0x200 / 4] = th1520 ? BIT(8) | BIT(11) | BIT(7) : 0;
	m->regs[0x204 / 4] = 0x02c02320;
	m->regs[0x304 / 4] = 1;
	m->regs[0x310 / 4] = 3;
	return (struct xuantie_cpu_clock){
		.pll = 0,
		.mux = 0x200,
		.backup = 0x204,
		.gmac = 0x100,
		.ccu = 0x300,
		.th1520 = th1520,
		.timer_hz = 1000000,
		.context = m,
		.read32 = read32,
		.write32 = write32,
		.ticks = ticks,
		.delay_us = delay_us,
	};
}

ZTEST(cpu_pll, test_a210_switches_and_preserves_other_fields)
{
	struct model m;
	struct xuantie_cpu_clock c = init(&m, false);
	uint32_t hz;

	zassert_ok(xuantie_cpu_clock_prepare(&c));
	zassert_equal(m.regs[0x204 / 4] & ~(GENMASK(15, 12) | 7),
		      0x02c02320 & ~(GENMASK(15, 12) | 7));
	zassert_ok(xuantie_cpu_clock_set_rate(&c, 500000000));
	zassert_ok(xuantie_cpu_clock_get_rate(&c, &hz));
	zassert_equal(hz, 500000000);
	zassert_ok(xuantie_cpu_clock_set_rate(&c, 1698000000));
	zassert_ok(xuantie_cpu_clock_get_rate(&c, &hz));
	zassert_equal(hz, 1698000000);
	zassert_equal(m.regs[0x200 / 4] & 1, 0);
}

ZTEST(cpu_pll, test_a210_lock_failure_restores_previous_clock)
{
	struct model m;
	struct xuantie_cpu_clock c = init(&m, false);
	uint32_t hz;

	zassert_ok(xuantie_cpu_clock_prepare(&c));
	m.fail_new = true;
	m.time = UINT32_MAX - 100;
	zassert_equal(xuantie_cpu_clock_set_rate(&c, 1200000000), -ETIMEDOUT);
	zassert_false(c.faulted);
	zassert_ok(xuantie_cpu_clock_get_rate(&c, &hz));
	zassert_equal(hz, 1896000000);
	zassert_equal(m.regs[0], m.original);
}

ZTEST(cpu_pll, test_a210_failed_restore_keeps_backup)
{
	struct model m;
	struct xuantie_cpu_clock c = init(&m, false);
	uint32_t hz;
	unsigned int writes;

	zassert_ok(xuantie_cpu_clock_prepare(&c));
	m.fail_new = m.fail_restore = true;
	zassert_equal(xuantie_cpu_clock_set_rate(&c, 1200000000), -ETIMEDOUT);
	zassert_true(c.faulted);
	zassert_ok(xuantie_cpu_clock_get_rate(&c, &hz));
	zassert_equal(hz, 500000000);
	writes = m.writes;
	zassert_equal(xuantie_cpu_clock_set_rate(&c, 1896000000), -EIO);
	zassert_equal(m.writes, writes);
}

ZTEST(cpu_pll, test_th1520_alternates_plls_and_limits_bus_clock)
{
	struct model m;
	struct xuantie_cpu_clock c = init(&m, true);
	uint32_t hz;

	zassert_ok(xuantie_cpu_clock_prepare(&c));
	zassert_ok(xuantie_cpu_clock_set_rate(&c, 1848000000));
	zassert_equal(m.regs[0x200 / 4] & 1, 1);
	zassert_equal((m.regs[0x200 / 4] >> 8) & 7, 2);
	zassert_equal(m.regs[0], m.original);
	zassert_ok(xuantie_cpu_clock_get_rate(&c, &hz));
	zassert_equal(hz, 1848000000);
	zassert_ok(xuantie_cpu_clock_set_rate(&c, 300000000));
	zassert_equal(m.regs[0x200 / 4] & 1, 0);
	zassert_equal((m.regs[0x200 / 4] >> 8) & 7, 1);
	zassert_true(m.regs[0x200 / 4] & BIT(7));
	zassert_ok(xuantie_cpu_clock_get_rate(&c, &hz));
	zassert_equal(hz, 300000000);
}

ZTEST(cpu_pll, test_th1520_lock_failure_does_not_switch_active_pll)
{
	struct model m;
	struct xuantie_cpu_clock c = init(&m, true);
	uint32_t hz;

	m.fail_new = true;
	zassert_equal(xuantie_cpu_clock_set_rate(&c, 1848000000), -ETIMEDOUT);
	zassert_equal(m.regs[0x200 / 4] & 1, 0);
	zassert_equal((m.regs[0x200 / 4] >> 8) & 7, 1);
	zassert_ok(xuantie_cpu_clock_get_rate(&c, &hz));
	zassert_equal(hz, 1500000000);
}
ZTEST(cpu_pll, test_fabric_limits_with_fixed_parent_clocks)
{
	struct model m;
	struct xuantie_cpu_clock c = init(&m, false);
	uint32_t backup;

	c.fabric = 0x800;
	c.video = 0x600;
	m.regs[0x600 / 4] = CFG(1, 110, 2, 1);
	m.regs[0x604 / 4] = BIT(24) | BIT(25);
	m.regs[0x204 / 4] = 0x00c02000;
	m.regs[0x208 / 4] = 0x00c00000;
	for (unsigned int offset = 0; offset <= 0x400; offset += 0x200) {
		m.regs[(c.fabric + offset + 4) / 4] = 1;
		m.regs[(c.fabric + offset + 0x10) / 4] = 3;
	}
	zassert_ok(xuantie_cpu_clock_prepare(&c));
	backup = m.regs[0x204 / 4] & ~(15U << 20);
	zassert_ok(xuantie_cpu_clock_set_rate(&c, 500000000));
	zassert_equal((m.regs[0x208 / 4] >> 20) & 15, 14);
	zassert_equal((m.regs[0x204 / 4] >> 20) & 15, 12);
	zassert_equal(m.regs[0x81c / 4], 5);
	zassert_equal(m.regs[0xa1c / 4], 7);
	zassert_equal(m.regs[0xc1c / 4], 7);
	zassert_equal(m.regs[0x204 / 4] & ~(15U << 20), backup);
	zassert_ok(xuantie_cpu_clock_set_rate(&c, 1896000000));
	zassert_equal((m.regs[0x208 / 4] >> 20) & 15, 2);
	zassert_equal((m.regs[0x204 / 4] >> 20) & 15, 3);
	zassert_equal(m.regs[0x81c / 4], 1);
	zassert_equal(m.regs[0xa1c / 4], 3);
	zassert_equal(m.regs[0xc1c / 4], 3);
}
ZTEST_SUITE(cpu_pll, NULL, NULL, NULL, NULL, NULL);
