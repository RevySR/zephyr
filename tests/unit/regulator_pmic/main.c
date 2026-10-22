/* SPDX-License-Identifier: Apache-2.0 */
#include "pmic_backend.h"
#include "coupled_backend.h"
#include <zephyr/ztest.h>
#define assert(condition) zassert_true(condition, "assertion failed")
#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct {
	uint8_t reg[128][0x3200];
	unsigned int writes, reads, width;
	bool fail_write, check_coupling;
} model;

static int read_block(void *context, uint8_t addr, uint16_t reg, unsigned int width, uint8_t *data,
		      size_t size, bool restart)
{
	(void)context;
	assert(addr < 128 && reg + size <= 0x3200);
	model.reads++;
	model.width = width;
	assert(!restart);
	memcpy(data, &model.reg[addr][reg], size);
	return 0;
}

static int write_block(void *context, uint8_t addr, uint16_t reg, unsigned int width,
		       const uint8_t *data, size_t size)
{
	(void)context;
	assert(addr < 128 && reg + size <= 0x3200);
	model.writes++;
	model.width = width;
	if (model.fail_write) {
		return -EIO;
	}
	memcpy(&model.reg[addr][reg], data, size);
	if (model.check_coupling) {
		int core = 300000 + model.reg[0x5a][0xa4] * 10000;
		int mem = 800000 + model.reg[0x5a][0xa7] * 20000;

		assert(core - mem < 50000 && core - mem >= -300000);
	}
	return 0;
}

static int delay(void *context, uint32_t us)
{
	(void)context;
	assert(us <= 20000);
	return 0;
}

static const struct pmic_backend_io io = {NULL, read_block, write_block, delay};

static void prepare(struct pmic_rail *r)
{
	unsigned int address = r->pmic->address;

	memset(&model, 0, sizeof(model));
	model.reg[0x5b][0x81] = 0x61;
	if (r->output == 17) {
		model.reg[0x5b][0x0d] = 8;
	}
	if (r->output == 18) {
		model.reg[0x5b][0x0d] = 0x80;
	}
	if (r->pmic->driver == &pmic_da9121_driver) {
		model.reg[address][0x48] = 5;
		model.reg[address][0x49] = 0x21;
	}
	model.reg[address][r->enable_reg] = r->enable_mask;
	if (r->pmic->driver == &pmic_slg51000_driver) {
		model.reg[address][r->voltage_reg + 0x61] = 255;
		model.reg[address][r->voltage_reg + 0x64] = r->slg_high_range ? 1 : 0;
	}
	model.reg[address][r->voltage_reg] =
		(uint8_t)(((r->min_uv - r->base_uv) / r->step_uv) << r->shift);
}

static void exercise(const struct pmic_driver *driver, unsigned int output, int32_t low,
		     int32_t high, bool high_range)
{
	struct pmic_backend p = {&io, driver, 0x5a, 0x5b};
	struct pmic_rail rail;
	int32_t value = 0;
	bool enabled;

	assert(pmic_rail_init(&rail, &p, output, low, high, 2000, true, high_range) == 0);
	prepare(&rail);
	assert(pmic_backend_voltage_ops.get_level(&rail, &value) == 0 && value == low);
	if (driver == &pmic_slg51000_driver) {
		assert(model.width == 2);
	}
	assert(pmic_backend_voltage_ops.set_level(&rail, high) == 0);
	assert(pmic_backend_voltage_ops.get_level(&rail, &value) == 0 && value == high);
	unsigned int writes = model.writes;

	assert(pmic_backend_voltage_ops.set_level(&rail, high + 1) == -ERANGE);
	assert(model.writes == writes);
	assert(pmic_backend_voltage_ops.set_enabled(&rail, false) == -EACCES);
	assert(pmic_backend_voltage_ops.get_enabled(&rail, &enabled) == 0 && enabled);
}

static void test_coupled(void)
{
	struct pmic_backend p = {&io, &pmic_da9063_driver, 0x5a, 0x5b};
	struct pmic_rail r[2];

	assert(pmic_rail_init(&r[0], &p, 17, 800000, 1000000, 250, true, false) == 0);
	assert(pmic_rail_init(&r[1], &p, 4, 800000, 1000000, 250, true, false) == 0);
	prepare(&r[0]);
	model.reg[0x5a][0x24] = 1;
	model.check_coupling = true;
	struct pmic_output d[2] = {
		{"core", 800000, 1000000, 10000, true, &pmic_backend_voltage_ops, &r[0]},
		{"mem", 800000, 1000000, 20000, true, &pmic_backend_voltage_ops, &r[1]},
	};
	const struct regulator_coupling c = {0, 1, -300000, 40000};
	struct regulator_coupled_group group = {{&d[0], &d[1]}, 2, &c, 1, false};
	int32_t value;

	assert(regulator_coupled_ops.set_level(&group, 1000000) == 0);
	assert(model.writes == 3);
	assert(regulator_coupled_ops.get_level(&group, &value) == 0 && value == 1000000);
	assert(regulator_coupled_ops.set_level(&group, 800000) == 0);
	assert(model.writes == 5);
	assert(regulator_coupled_ops.set_level(&group, 810000) == -ERANGE);
	assert(model.writes == 5);
	model.fail_write = true;
	assert(regulator_coupled_ops.set_level(&group, 1000000) == -EIO && group.faulted);
	model.fail_write = false;
	assert(regulator_coupled_ops.set_level(&group, 1000000) == -EIO);
	assert(model.writes == 6);
}

static void test_guards(void)
{
	struct pmic_backend p = {&io, &pmic_da9063_driver, 0x5a, 0x5b};
	struct pmic_rail r;
	int32_t uv = 123;

	assert(pmic_rail_init(&r, &p, 17, 800000, 1000000, 250, true, false) == 0);
	prepare(&r);
	model.reg[0x5b][0x0d] = 0;
	assert(pmic_backend_voltage_ops.set_level(&r, 900000) == -ENOTSUP && !model.writes);
	model.reg[0x5b][0x0d] = 8;
	model.reg[0x5b][0x81] = 0;
	assert(pmic_backend_voltage_ops.get_level(&r, &uv) == -ENODEV && uv == 123);
	model.reg[0x5b][0x81] = 0x61;
	model.reg[0x5a][0x32] = 1;
	assert(pmic_backend_voltage_ops.set_level(&r, 900000) == -ENOTSUP && !model.writes);
	model.reg[0x5a][0x32] = 0x80; /* Unrelated LDO3 bank must not block BCORE1. */
	assert(pmic_backend_voltage_ops.get_level(&r, &uv) == 0 && uv == 800000);
	assert(pmic_rail_init(&r, &p, 10, 900000, 3600000, 2000, true, false) == 0);
	prepare(&r);
	model.reg[0x5a][0x2a] = 0x81; /* LDO_CONF, not the buck's bit 3. */
	assert(pmic_backend_voltage_ops.get_level(&r, &uv) == -ENOTSUP);
	model.reg[0x5a][0x2a] = 0x11; /* LDO5 bank B selected. */
	assert(pmic_backend_voltage_ops.get_level(&r, &uv) == -ENOTSUP);
	model.reg[0x5a][0x2a] = 0x09; /* Pull-down disable does not select bank B. */
	assert(pmic_backend_voltage_ops.get_level(&r, &uv) == 0 && uv == 900000);
	p.driver = &pmic_slg51000_driver;
	assert(pmic_rail_init(&r, &p, 0, 2400000, 3475000, 2000, true, true) == 0);
	prepare(&r);
	model.reg[0x5a][0x2064] = 0;
	assert(pmic_backend_voltage_ops.get_level(&r, &uv) == -ENOTSUP);
	model.reg[0x5a][0x2064] = 1;
	model.reg[0x5a][0x2060] = 1;
	assert(pmic_backend_voltage_ops.set_level(&r, 2500000) == -ERANGE && !model.writes);
}

ZTEST(regulator_pmic, test_contract)
{
	unsigned int configurations = 0;

	for (unsigned int i = 0; i < 19; i++) {
		int32_t low, high;

		if (i < 2 || i == 17) {
			low = 300000;
			high = 1570000;
		} else if (i == 2) {
			low = 530000;
			high = 1800000;
		} else if (i < 6 || i == 18) {
			low = 800000;
			high = 3340000;
		} else if (i == 6 || i == 7 || i == 9) {
			low = 600000;
			high = 1860000;
		} else if (i == 8) {
			low = 900000;
			high = 3440000;
		} else {
			low = 900000;
			high = 3600000;
		}
		exercise(&pmic_da9063_driver, i, low, high, false);
		configurations++;
	}
	exercise(&pmic_da9121_driver, 0, 300000, 1900000, false);
	configurations++;
	for (unsigned int i = 0; i < 7; i++) {
		int32_t low = i < 2 ? 2400000 : (i == 4 || i == 5 ? 400000 : 1200000);
		int32_t high = i < 2 ? 3675000 : (i == 4 || i == 5 ? 1675000 : 3600000);

		exercise(&pmic_slg51000_driver, i, low, high, i < 2);
		configurations++;
	}
	for (unsigned int i = 0; i < 2; i++) {
		exercise(&pmic_slg51000_driver, i, 1200000, 2475000, false);
		configurations++;
	}
	test_coupled();
	test_guards();
	printf("PASS: %u PMIC output/range configurations and coupled ramp invariants\n",
	       configurations);
}

ZTEST_SUITE(regulator_pmic, NULL, NULL, NULL, NULL, NULL);
