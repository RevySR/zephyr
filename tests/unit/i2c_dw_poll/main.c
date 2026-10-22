/* SPDX-License-Identifier: Apache-2.0 */
#include "i2c_dw_poll_core.h"
#include <zephyr/ztest.h>
#define assert(condition) zassert_true(condition, "assertion failed")
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct controller_model {
	uint32_t regs[64], tick, writes, command_count, commands[8];
	bool stopped_timer, nack, stuck_enable, no_stop;
};

static uint32_t read32(void *context, uint32_t offset)
{
	struct controller_model *m = context;

	assert(offset < 256 && !(offset & 3));
	if (offset == 0x40) {
		m->regs[0x34 / 4] = 0;
	}
	if (offset == 0x60) {
		m->regs[0x34 / 4] &= ~(1U << 9);
	}
	if (offset == 0x10) {
		m->regs[0x70 / 4] &= ~(1U << 3);
		return 0x5a;
	}
	return m->regs[offset / 4];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
	struct controller_model *m = context;

	assert(offset < 256 && !(offset & 3));
	m->writes++;
	m->regs[offset / 4] = value;
	if (offset == 0x6c && !m->stuck_enable) {
		m->regs[0x9c / 4] = value;
	}
	if (offset == 0x10) {
		assert(m->command_count < 8);
		m->commands[m->command_count++] = value;
		if (m->nack) {
			m->regs[0x34 / 4] |= 1U << 6;
			m->regs[0x80 / 4] = 1;
		} else {
			if (value & (1U << 8)) {
				m->regs[0x70 / 4] |= 1U << 3;
			}
			if ((value & (1U << 9)) && !m->no_stop) {
				m->regs[0x34 / 4] |= 1U << 9;
			}
		}
	}
}

static uint32_t ticks(void *context)
{
	struct controller_model *m = context;

	if (!m->stopped_timer) {
		m->tick += 3;
	}
	return m->tick;
}

static void reset_model(struct controller_model *m)
{
	memset(m, 0, sizeof(*m));
	m->regs[0xfc / 4] = 0x44570140;
	m->regs[0] = 0x63;
	m->regs[0x14 / 4] = 120;
	m->regs[0x18 / 4] = 120;
	m->regs[0x70 / 4] = 2;
}

ZTEST(i2c_dw_poll, test_contract)
{
	struct controller_model model;
	struct dw_i2c_poll bus = {&model, read32, write32, ticks, 3, 100, false};
	uint8_t value;

	reset_model(&model);
	model.tick = UINT32_MAX - 1;
	assert(dw_i2c_poll_adopt(&bus) == 0);
	assert(model.writes == 0);
	assert(dw_i2c_poll_write8(&bus, 6, 0x0e, 0x98) == 0);
	assert(model.command_count == 2 && model.commands[0] == 0x0e && model.commands[1] == 0x298);
	assert(model.regs[0x9c / 4] == 0);
	assert(dw_i2c_poll_read8(&bus, 0x67, 0x7c, &value) == 0 && value == 0x5a);
	assert(model.command_count == 4 && model.commands[2] == 0x27c &&
	       model.commands[3] == 0x300);
	assert(model.regs[0x14 / 4] == 120 && model.regs[0x18 / 4] == 120);
	assert(dw_i2c_poll_write8(&bus, 128, 0, 0) == -EINVAL);

	reset_model(&model);
	model.stopped_timer = true;
	assert(dw_i2c_poll_adopt(&bus) == -ETIMEDOUT && model.writes == 0);
	reset_model(&model);
	model.regs[0x70 / 4] |= 1U << 5;
	assert(dw_i2c_poll_adopt(&bus) == -EBUSY && model.writes == 0);
	reset_model(&model);
	model.regs[0x30 / 4] = 1;
	assert(dw_i2c_poll_adopt(&bus) == -EBUSY && model.writes == 0);
	reset_model(&model);
	model.regs[0x14 / 4] = 0;
	assert(dw_i2c_poll_adopt(&bus) == -ENODEV && model.writes == 0);
	reset_model(&model);
	model.regs[0] |= 1U << 4; /* A 10-bit master cannot use these 7-bit PMIC addresses. */
	assert(dw_i2c_poll_adopt(&bus) == -ENODEV && model.writes == 0);
	reset_model(&model);
	assert(dw_i2c_poll_adopt(&bus) == 0);
	model.nack = true;
	assert(dw_i2c_poll_write8(&bus, 6, 0x0e, 1) == -ENXIO);
	assert(!bus.ready && model.command_count == 1);
	assert(dw_i2c_poll_write8(&bus, 6, 0x0e, 1) == -ENODEV && model.command_count == 1);
	reset_model(&model);
	assert(dw_i2c_poll_adopt(&bus) == 0);
	model.stuck_enable = true;
	assert(dw_i2c_poll_read8(&bus, 6, 0x0e, &value) == -ETIMEDOUT);
	assert(!bus.ready && model.command_count == 0);
	reset_model(&model);
	assert(dw_i2c_poll_adopt(&bus) == 0);
	model.no_stop = true;
	assert(dw_i2c_poll_write8(&bus, 6, 0x0e, 1) == -ETIMEDOUT);
	assert(!bus.ready && model.command_count == 2);
	reset_model(&model);
	assert(dw_i2c_poll_adopt(&bus) == 0);
	uint8_t block[4];

	assert(dw_i2c_poll_read_reg(&bus, 0x75, 0x2060, 2, block, sizeof(block), true) == 0);
	assert(model.commands[0] == 0x20 && model.commands[1] == 0x60);
	assert(model.commands[2] == 0x500 && model.commands[5] == 0x300);
	assert(block[0] == 0x5a && block[3] == 0x5a);
	reset_model(&model);
	assert(dw_i2c_poll_adopt(&bus) == 0);
	const uint8_t command[] = {1, 0, 12, 53, 0};

	assert(dw_i2c_poll_write_reg(&bus, 0x50, 3, 1, command, sizeof(command)) == 0);
	assert(model.command_count == 6 && model.commands[0] == 3 && model.commands[5] == 0x200);
	uint32_t writes = model.writes;

	assert(dw_i2c_poll_write_reg(&bus, 0x50, 0x100, 1, command, sizeof(command)) == -EINVAL);
	assert(dw_i2c_poll_read_reg(&bus, 0x50, 0, 2, block, 9, true) == -EINVAL);
	assert(model.writes == writes);
	bus.ticks_per_us = UINT32_MAX;
	assert(dw_i2c_poll_delay(&bus, 20000) == -EINVAL);
	puts("PASS: DW I2C byte/STOP sequences, inherited timing, timer wrap, NACK and bounded "
	     "failure");
}

ZTEST_SUITE(i2c_dw_poll, NULL, NULL, NULL, NULL, NULL);
