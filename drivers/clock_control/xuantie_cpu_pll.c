/* SPDX-License-Identifier: Apache-2.0 */
#include "xuantie_cpu_pll.h"
#include <zephyr/sys/util.h>
#include <errno.h>

#define CFG_MASK             (GENMASK(26, 24) | GENMASK(22, 20) | GENMASK(19, 8) | GENMASK(5, 0))
#define CFG(ref, fb, p1, p2) ((ref) | ((fb) << 8) | ((p1) << 20) | ((p2) << 24))
#define INTEGER_MODE         (BIT(24) | BIT(25))

static uint32_t read32(const struct xuantie_cpu_clock *c, uintptr_t reg)
{
	return c->read32(c->context, reg);
}
static void write32(const struct xuantie_cpu_clock *c, uint32_t value, uintptr_t reg)
{
	c->write32(c->context, value, reg);
}
static void update(const struct xuantie_cpu_clock *c, uintptr_t reg, uint32_t mask, uint32_t value)
{
	write32(c, (read32(c, reg) & ~mask) | value, reg);
}
static void delay(const struct xuantie_cpu_clock *c, uint32_t us)
{
	c->delay_us(c->context, us);
}
static int wait_lock(const struct xuantie_cpu_clock *c, uintptr_t reg, uint32_t mask)
{
	uint32_t start = c->ticks(c->context);
	uint32_t ticks = DIV_ROUND_UP((uint64_t)c->timer_hz * 10000, 1000000);

	do {
		if (read32(c, reg) & mask) {
			delay(c, 30);
			return 0;
		}
	} while ((uint32_t)(c->ticks(c->context) - start) < ticks);
	return -ETIMEDOUT;
}

static uint32_t pll_rate(const struct xuantie_cpu_clock *c, uintptr_t base, bool postdiv,
			 uint32_t reset, uint32_t bypass)
{
	uint32_t cfg = read32(c, base), control = read32(c, base + 4);
	uint32_t ref = cfg & 0x3f, div = 1;
	uint64_t feedback = (uint64_t)((cfg >> 8) & 0xfff) << 24;

	if (control & reset) {
		return 0;
	}
	if (control & bypass) {
		return 24000000;
	}
	if (!ref) {
		return 0;
	}
	if (!(control & BIT(24))) {
		feedback += control & 0xffffff;
	}
	if (postdiv) {
		div = ((cfg >> 20) & 7) * ((cfg >> 24) & 7);
	}
	return div ? ((feedback * 24000000 / ref) >> 24) / div : 0;
}

int xuantie_cpu_clock_get_rate(struct xuantie_cpu_clock *c, uint32_t *hz)
{
	uint32_t select = read32(c, c->mux);
	uintptr_t pll = c->pll;

	if (c->th1520) {
		if (!(select & 1) && (select & 2)) {
			*hz = 24000000;
			return 0;
		}
		pll += (select & 1) ? 0x10 : 0;
		*hz = pll_rate(c, pll, true, BIT(29), BIT(30));
	} else {
		if (select & 1) {
			uint32_t backup = read32(c, c->backup);
			uint32_t div = (backup >> 12) & 15;

			if ((backup & 7) != 4 || !div) {
				return -ENOTSUP;
			}
			*hz = pll_rate(c, c->gmac, false, BIT(30), BIT(31)) / div;
		} else {
			*hz = pll_rate(c, pll, false, BIT(30), BIT(31));
		}
		if (read32(c, c->ccu + 0x1c) & 0xffff) {
			return -ENOTSUP;
		}
	}
	return *hz ? 0 : -EIO;
}

static uint32_t th1520_config(uint32_t hz)
{
	static const struct {
		uint32_t hz;
		uint16_t fb;
		uint8_t p1, p2;
	} rates[] = {
		{300000000, 125, 5, 2},  {400000000, 100, 3, 2},  {500000000, 125, 6, 1},
		{600000000, 125, 5, 1},  {702000000, 117, 4, 1},  {800000000, 100, 3, 1},
		{900000000, 75, 2, 1},   {1000000000, 125, 3, 1}, {1104000000, 92, 2, 1},
		{1200000000, 100, 2, 1}, {1296000000, 108, 2, 1}, {1404000000, 117, 2, 1},
		{1500000000, 125, 2, 1}, {1608000000, 67, 1, 1},  {1704000000, 71, 1, 1},
		{1848000000, 77, 1, 1},
	};

	for (size_t i = 0; i < ARRAY_SIZE(rates); i++) {
		if (rates[i].hz == hz) {
			return CFG(1, rates[i].fb, rates[i].p1, rates[i].p2);
		}
	}
	return 0;
}

static void th1520_bus_div(const struct xuantie_cpu_clock *c, uint32_t div)
{
	update(c, c->mux, GENMASK(10, 8) | BIT(11), (div - 1) << 8);
	delay(c, 1);
	update(c, c->mux, BIT(11), BIT(11));
	delay(c, 1);
}

/*
 * The vendor OPPs specify fabric rate ceilings. Keep the boot-selected
 * VIDEO/GMAC sources and round divisors up, so shared PLL users are unaffected.
 */
static int a210_fabric_set_rate(struct xuantie_cpu_clock *c, uint32_t hz)
{
	static const struct {
		uint32_t hz, bus, pic, cfg, apb;
	} limits[] = {
		{500000000, 196608000, 250000000, 110000000, 82500000},
		{1200000000, 660000000, 500000000, 220000000, 165000000},
		{1500000000, 1000000000, 1000000000, 330000000, 165000000},
		{1698000000, 1100000000, 1000000000, 330000000, 165000000},
		{1896000000, 1320000000, 1000000000, 330000000, 165000000},
	};
	uintptr_t top = c->backup - 4;
	uint32_t video, gmac, peripheral, bus_div, pic_div, cfg_div, apb_div;
	size_t i;

	if (!c->fabric) {
		return 0;
	}
	for (i = 0; i < ARRAY_SIZE(limits); i++) {
		if (limits[i].hz == hz) {
			break;
		}
	}
	if (i == ARRAY_SIZE(limits)) {
		return -ERANGE;
	}
	if ((read32(c, top + 8) & GENMASK(17, 16)) || (read32(c, top + 4) & BIT(3))) {
		return -ENOTSUP;
	}
	video = pll_rate(c, c->video, false, BIT(30), BIT(31));
	gmac = pll_rate(c, c->gmac, false, BIT(30), BIT(31));
	peripheral = pll_rate(c, c->video, true, BIT(30), BIT(31)) / 2;
	if (!video || !gmac || !peripheral) {
		return -EIO;
	}
	bus_div = DIV_ROUND_UP(video, limits[i].bus);
	pic_div = DIV_ROUND_UP(gmac, limits[i].pic);
	cfg_div = DIV_ROUND_UP(peripheral, limits[i].cfg);
	apb_div = DIV_ROUND_UP(peripheral, limits[i].apb);
	if (bus_div < 2 || bus_div > 15 || pic_div < 3 || pic_div > 15 || cfg_div < 2 ||
	    cfg_div > 6 || apb_div < 4 || apb_div > 8) {
		return -ERANGE;
	}
	for (unsigned int offset = 0; offset <= 0x400; offset += 0x200) {
		if (!(read32(c, c->fabric + offset + 4) & 1) ||
		    (read32(c, c->fabric + offset + 0x10) & 3) != 3) {
			return -ENOTSUP;
		}
	}
	update(c, top + 8, GENMASK(23, 20), bus_div << 20);
	update(c, top + 4, GENMASK(23, 20), pic_div << 20);
	update(c, c->fabric + 0x1c, 0xffff, cfg_div - 1);
	update(c, c->fabric + 0x21c, 0xffff, apb_div - 1);
	update(c, c->fabric + 0x41c, 0xffff, apb_div - 1);
	delay(c, 1);
	if (((read32(c, top + 8) >> 20) & 15) != bus_div ||
	    ((read32(c, top + 4) >> 20) & 15) != pic_div ||
	    (read32(c, c->fabric + 0x1c) & 0xffff) != cfg_div - 1 ||
	    (read32(c, c->fabric + 0x21c) & 0xffff) != apb_div - 1 ||
	    (read32(c, c->fabric + 0x41c) & 0xffff) != apb_div - 1) {
		c->faulted = true;
		return -EIO;
	}
	return 0;
}

int xuantie_cpu_clock_set_rate(struct xuantie_cpu_clock *c, uint32_t hz)
{
	uintptr_t pll = c->pll, status;
	uint32_t cfg, reset, bypass, lock, old0, old1, old_select;
	uint32_t current, old_hz, old_div = 0, new_div = 0;
	int ret;

	if (c->faulted) {
		return -EIO;
	}
	ret = xuantie_cpu_clock_get_rate(c, &current);
	if (ret) {
		return ret;
	}
	old_hz = current;
	if (hz <= current) {
		ret = a210_fabric_set_rate(c, hz);
		if (ret) {
			return ret;
		}
	}
	if (hz == current) {
		return 0;
	}
	old_select = read32(c, c->mux);
	if (c->th1520) {
		cfg = th1520_config(hz);
		if (!cfg) {
			return -ERANGE;
		}
		/* Retune the inactive PLL, leaving the running CPU clock intact. */
		pll += (old_select & 1) ? 0 : 0x10;
		status = c->pll + 0x80;
		lock = (old_select & 1) ? BIT(1) : BIT(4);
		reset = BIT(29);
		bypass = BIT(30);
		old_div = ((old_select >> 8) & 7) + 1;
		new_div = MAX(2U, DIV_ROUND_UP(hz, 750000000U));
		if (new_div > old_div) {
			th1520_bus_div(c, new_div);
		}
	} else {
		if (hz == 500000000) {
			update(c, c->mux, BIT(0), BIT(0));
			delay(c, 10);
			return (read32(c, c->mux) & 1) ? 0 : -EIO;
		}
		switch (hz) {
		case 1200000000:
			cfg = CFG(1, 50, 1, 1);
			break;
		case 1500000000:
			cfg = CFG(2, 125, 1, 1);
			break;
		case 1698000000:
			cfg = CFG(4, 283, 1, 1);
			break;
		case 1896000000:
			cfg = CFG(1, 79, 1, 1);
			break;
		default:
			return -ERANGE;
		}
		status = pll + 0x10;
		lock = BIT(0);
		reset = BIT(30);
		bypass = BIT(31);
		update(c, c->mux, BIT(0), BIT(0));
		if (!(read32(c, c->mux) & 1)) {
			return -EIO;
		}
		delay(c, 10);
	}
	old0 = read32(c, pll);
	old1 = read32(c, pll + 4);
	update(c, pll + 4, reset, reset);
	update(c, pll, CFG_MASK, cfg);
	update(c, pll + 4, 0xffffff | INTEGER_MODE | bypass, INTEGER_MODE);
	delay(c, 3);
	update(c, pll + 4, reset, 0);
	ret = wait_lock(c, status, lock);
	if (ret) {
		/* Retry the previous PLL state, retaining the alternate clock on failure. */
		update(c, pll + 4, reset, reset);
		write32(c, old0, pll);
		write32(c, old1, pll + 4);
		if (!c->th1520 && wait_lock(c, status, lock)) {
			c->faulted = true;
			return ret;
		}
		update(c, c->mux, BIT(0), old_select & BIT(0));
		if (c->th1520 && new_div > old_div) {
			th1520_bus_div(c, old_div);
		}
		return ret;
	}
	if (c->th1520) {
		/* PLL0 path must select PLL0 rather than the oscillator. */
		update(c, c->mux, BIT(0) | BIT(1), (old_select & 1) ? 0 : 1);
		if (new_div < old_div) {
			th1520_bus_div(c, new_div);
		}
	} else {
		update(c, c->mux, BIT(0), 0);
	}
	delay(c, 10);
	ret = xuantie_cpu_clock_get_rate(c, &current);
	if (ret || current != hz) {
		return ret ? ret : -EIO;
	}
	return hz > old_hz ? a210_fabric_set_rate(c, hz) : 0;
}

int xuantie_cpu_clock_prepare(struct xuantie_cpu_clock *c)
{
	uint32_t hz;
	int ret = xuantie_cpu_clock_get_rate(c, &hz);

	if (ret || !c->timer_hz) {
		return ret ? ret : -EINVAL;
	}
	if (!c->th1520) {
		uint32_t gmac = pll_rate(c, c->gmac, false, BIT(30), BIT(31));
		uint32_t div = gmac / 500000000;

		if ((read32(c, c->mux) & 1) || gmac % 500000000 || div < 2 || div > 15 ||
		    !(read32(c, c->ccu + 4) & 1) || (read32(c, c->ccu + 0x10) & 3) != 3) {
			return -ENOTSUP;
		}
		/* Startup only: AP clients are quiescent and the CPU PLL is selected. */
		update(c, c->backup, GENMASK(15, 12) | GENMASK(2, 0), (div << 12) | 4);
	}
	return 0;
}
