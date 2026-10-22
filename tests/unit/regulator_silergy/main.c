/* SPDX-License-Identifier: Apache-2.0 */
#include "silergy_backend.h"
#include <zephyr/mgmt/scmi/voltage.h>
#include <zephyr/ztest.h>
#include <zephyr/dt-bindings/regulator/sy70209.h>
#define assert(condition) zassert_true(condition, "assertion failed")
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int bridge_get_level(void *c, int32_t *v)
{
	return silergy_voltage_ops.get_level(c, v);
}
static int bridge_set_level(void *c, int32_t v)
{
	return silergy_voltage_ops.set_level(c, v);
}
static int bridge_get_enabled(void *c, bool *v)
{
	return silergy_voltage_ops.get_enabled(c, v);
}
static int bridge_set_enabled(void *c, bool v)
{
	return silergy_voltage_ops.set_enabled(c, v);
}
static const struct scmi_voltage_ops scmi_ops = {bridge_get_level, bridge_set_level,
						 bridge_get_enabled, bridge_set_enabled};

struct device_model {
	uint8_t registers[128][256];
	unsigned int reads, writes, delays;
	uint32_t last_delay;
	int read_error, write_error, delay_error;
	bool ignore_write;
};

static int read8(void *context, uint8_t address, uint8_t reg, uint8_t *value)
{
	struct device_model *m = context;

	m->reads++;
	if (!m->read_error) {
		*value = m->registers[address][reg];
	}
	return m->read_error;
}

static int write8(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
	struct device_model *m = context;

	m->writes++;
	if (!m->write_error && !m->ignore_write) {
		m->registers[address][reg] = value;
	}
	return m->write_error;
}

static int delay(void *context, uint32_t us)
{
	struct device_model *m = context;

	m->delays++;
	m->last_delay = us;
	return m->delay_error;
}

static size_t command(const struct scmi_server *server, uint8_t cmd, uint32_t domain, uint32_t arg,
		      uint32_t value, uint8_t *out)
{
	static const size_t lengths[] = {4, 4, 8, 8, 12, 12, 8, 16, 8};
	uint8_t request[16];

	scmi_put_le32(request, 0x05545c00U | cmd);
	scmi_put_le32(request + 4, domain);
	scmi_put_le32(request + 8, arg);
	scmi_put_le32(request + 12, value);
	return scmi_dispatch(server, request, cmd < 9 ? lengths[cmd] : 4, out, 32);
}

ZTEST(regulator_silergy, test_contract)
{
	struct device_model model = {0};
	const struct silergy_bus bus = {&model, read8, write8, delay};
	struct silergy_rail rails[] = {
		{.bus = &bus,
		 .type = SILERGY_SY70209,
		 .address = 6,
		 .voltage_reg = 0x0e,
		 .voltage_mask = 0x7f,
		 .enable_reg = 0x3d,
		 .enable_mask = 0x20,
		 .selector_base_uv = 600000,
		 .selector_step_uv = 12500,
		 .min_uv = 600000,
		 .max_uv = 1000000,
		 .ramp_uv_per_us = 100,
		 .enable_delay_us = 2000,
		 .always_on = true},
		{.bus = &bus,
		 .type = SILERGY_SY70300,
		 .address = 0x67,
		 .voltage_reg = 0x7c,
		 .voltage_mask = 0xff,
		 .enable_reg = 0x7d,
		 .enable_mask = 1,
		 .selector_base_uv = 450000,
		 .selector_step_uv = 5000,
		 .min_uv = 800000,
		 .max_uv = 1000000,
		 .ramp_uv_per_us = 100,
		 .enable_delay_us = 2000,
		 .always_on = true,
		 .parent_address = 6,
		 .parent_reg = 0x40,
		 .parent_mask = 4},
	};
	struct scmi_voltage_domain domains[] = {
		{"dvdd_cpu", 600000, 1000000, 12500, true, &scmi_ops, &rails[0]},
		{"dvdd_cpu_p", 800000, 1000000, 5000, true, &scmi_ops, &rails[1]},
	};
	struct scmi_server server = {{"aon-firmware", "test", 2}, domains, 2};
	uint8_t output[32], request[20] = {0};
	unsigned int writes, reads;

	zassert_ok(silergy_rail_init(&rails[0], 6));
	zassert_ok(silergy_rail_init(&rails[1], 2));
	model.registers[6][0x58] = 0x88;
	model.registers[6][0x0e] = 0x90; /* 800 mV selector 16, unrelated high bit set. */
	model.registers[6][0x3d] = 0x60;
	model.registers[6][0x40] = 4;
	model.registers[0x67][1] = 0xff;
	model.registers[0x67][2] = 1;
	model.registers[0x67][0x7c] = 70; /* 800 mV */
	model.registers[0x67][0x7d] = 0xa1;
	assert(command(&server, 0, 0, 0, 0, output) == 12);
	assert(scmi_get_le32(output + 8) == 0x10000);
	assert(command(&server, 1, 0, 0, 0, output) == 12);
	assert(scmi_get_le32(output + 8) == 2);
	assert(command(&server, 3, 0, 0, 0, output) == 28);
	assert(strcmp((char *)output + 12, "dvdd_cpu") == 0);
	assert(command(&server, 4, 0, 0, 0, output) == 24);
	assert(scmi_get_le32(output + 8) == 0x1003);
	assert(scmi_get_le32(output + 12) == 600000);
	assert(scmi_get_le32(output + 16) == 1000000);
	assert(scmi_get_le32(output + 20) == 12500);
	assert(command(&server, 4, 0, 1, 0, output) == 8);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_OUT_OF_RANGE);
	assert(command(&server, 8, 0, 0, 0, output) == 12);
	assert(scmi_get_le32(output + 8) == 800000);
	assert(command(&server, 7, 0, 0, 900000, output) == 8);
	assert(scmi_get_le32(output + 4) == 0);
	assert(model.registers[6][0x0e] == 0x98); /* Preserve bit 7, selector 24. */
	assert(model.last_delay == 3000);
	assert(command(&server, 7, 1, 0, 1000000, output) == 8);
	assert(scmi_get_le32(output + 4) == 0 && model.registers[0x67][0x7c] == 110);
	assert(model.last_delay == 4000 && model.registers[0x67][0x7d] == 0xa1);

	writes = model.writes;
	reads = model.reads;
	const uint32_t invalid_levels[] = {599999, 600001, 1000001, 0xffffffffU};

	for (size_t i = 0; i < ARRAY_SIZE(invalid_levels); i++) {
		command(&server, 7, 0, 0, invalid_levels[i], output);
		assert((int32_t)scmi_get_le32(output + 4) == SCMI_OUT_OF_RANGE);
	}
	command(&server, 5, 0, 0, 0, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_DENIED);
	command(&server, 7, 0, 1, 900000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_NOT_SUPPORTED);
	command(&server, 7, 0, 2, 900000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_INVALID_PARAMETERS);
	command(&server, 7, UINT32_MAX, 0, 900000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_NOT_FOUND);
	assert(model.writes == writes && model.reads == reads);

	/* All short and oversized LEVEL_SET requests are rejected before I/O. */
	scmi_put_le32(request, 0x05545c07);
	scmi_put_le32(request + 12, 800000);
	for (size_t size = 0; size <= sizeof(request); size++) {
		if (size == 16) {
			continue;
		}
		size_t response_size =
			scmi_dispatch(&server, request, size, output, sizeof(output));

		assert(response_size == (size < 4 ? 0U : 8U));
	}
	assert(scmi_dispatch(&server, request, 16, output, 7) == 0);
	assert(model.writes == writes && model.reads == reads);
	request[1] |= 1; /* Not a command message. */
	assert(scmi_dispatch(&server, request, 16, output, sizeof(output)) == 8);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_PROTOCOL_ERROR);
	assert(model.writes == writes && model.reads == reads);

	model.read_error = -ETIMEDOUT;
	command(&server, 8, 0, 0, 0, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_COMMS_ERROR);
	model.read_error = 0;
	model.registers[6][0x58] = 0;
	command(&server, 7, 0, 0, 800000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_HARDWARE_ERROR);
	assert(model.writes == writes);
	model.registers[6][0x58] = 0x88;
	model.delay_error = -ETIMEDOUT;
	command(&server, 7, 0, 0, 800000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_COMMS_ERROR);
	assert(model.writes == writes);
	model.delay_error = 0;
	model.ignore_write = true;
	command(&server, 7, 0, 0, 800000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_HARDWARE_ERROR);
	assert(model.writes == writes + 1); /* No replay of the failed write. */
	model.ignore_write = false;
	writes = model.writes;
	model.write_error = -ENXIO;
	command(&server, 7, 0, 0, 800000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_COMMS_ERROR);
	assert(model.writes == writes + 1 && model.registers[6][0x0e] == 0x98);
	model.write_error = 0;
	model.registers[6][0x40] = 0;
	writes = model.writes;
	command(&server, 6, 1, 0, 0, output);
	assert(scmi_get_le32(output + 4) == 0 && scmi_get_le32(output + 8) == 0);
	command(&server, 7, 1, 0, 850000, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_DENIED);
	command(&server, 5, 1, 7, 0, output);
	assert((int32_t)scmi_get_le32(output + 4) == SCMI_DENIED);
	assert(model.writes == writes && model.registers[6][0x40] == 0);

	/* Base advertises Voltage only when domains are configured. */
	scmi_put_le32(request, 0x4001);
	assert(scmi_dispatch(&server, request, 4, output, sizeof(output)) == 12);
	assert(scmi_get_le32(output + 8) == 0x101);
	scmi_put_le32(request, 0x4006);
	scmi_put_le32(request + 4, 0);
	assert(scmi_dispatch(&server, request, 8, output, sizeof(output)) == 16);
	assert(scmi_get_le32(output + 8) == 1 && scmi_get_le32(output + 12) == 0x17);
	scmi_put_le32(request + 4, 1);
	assert(scmi_dispatch(&server, request, 8, output, sizeof(output)) == 12);
	assert(scmi_get_le32(output + 8) == 0);
	puts("PASS: Voltage discovery, PMIC selectors, bounds, readback and shared-enable policy");
}

ZTEST(regulator_silergy, test_reject_invalid_configuration_before_io)
{
	struct device_model model = {0};
	const struct silergy_bus bus = {&model, read8, write8, delay};
	struct silergy_rail rail = {
		.bus = &bus,
		.type = SILERGY_SY70209,
		.address = 6,
		.min_uv = 600000,
		.max_uv = 1000000,
		.ramp_uv_per_us = 100,
		.enable_delay_us = 2000,
	};

	zassert_ok(silergy_rail_init(&rail, 6));
	rail.min_uv = 600001;
	zassert_equal(silergy_rail_init(&rail, 6), -ERANGE);
	rail.min_uv = 1000001;
	zassert_equal(silergy_rail_init(&rail, 6), -EINVAL);
	rail.min_uv = 600000;
	rail.max_uv = 3000000;
	zassert_equal(silergy_rail_init(&rail, 6), -ERANGE);
	rail.max_uv = 1000000;
	rail.ramp_uv_per_us = 0;
	zassert_equal(silergy_rail_init(&rail, 6), -EINVAL);
	rail.ramp_uv_per_us = 100;
	zassert_equal(silergy_rail_init(&rail, 9), -EINVAL);
	rail.type = SILERGY_SY70300;
	zassert_equal(silergy_rail_init(&rail, 4), -EINVAL);
	zassert_ok(silergy_rail_init(&rail, 2));
	zassert_equal(rail.voltage_reg, 0x7c);
	zassert_equal(rail.enable_reg, 0x7d);
	zassert_equal(model.reads + model.writes + model.delays, 0);
}

ZTEST(regulator_silergy, test_accelerator_gates_preserve_c920_supply)
{
	struct device_model model = {0};
	const struct silergy_bus bus = {&model, read8, write8, delay};
	struct silergy_rail rail = {
		.bus = &bus,
		.type = SILERGY_SY70300,
		.address = 0x67,
		.min_uv = 750000,
		.max_uv = 800000,
		.ramp_uv_per_us = 100,
		.enable_delay_us = 2000,
		.parent_address = 6,
		.parent_reg = 0x40,
		.parent_mask = 4,
	};
	bool enabled;

	model.registers[0x67][1] = 0xff;
	model.registers[0x67][2] = 1;
	model.registers[6][0x40] = 0xa4;
	model.registers[0x67][0x7d] = 0xb1;
	for (unsigned int output = 1; output <= 3; output += 2) {
		zassert_ok(silergy_rail_init(&rail, output));
		model.registers[0x67][rail.enable_reg] = 0xc0;
		zassert_ok(silergy_voltage_ops.set_enabled(&rail, true));
		zassert_ok(silergy_voltage_ops.get_enabled(&rail, &enabled));
		zassert_true(enabled);
		zassert_equal(model.registers[0x67][rail.enable_reg], 0xc1);
		zassert_ok(silergy_voltage_ops.set_enabled(&rail, false));
		zassert_ok(silergy_voltage_ops.get_enabled(&rail, &enabled));
		zassert_false(enabled);
		zassert_equal(model.registers[0x67][rail.enable_reg], 0xc0);
		zassert_equal(model.registers[0x67][0x7d], 0xb1);
		zassert_equal(model.registers[6][0x40], 0xa4);
	}
}

ZTEST(regulator_silergy, test_fixed_ldos_share_selector_bytes)
{
	struct device_model model = {0};
	const struct silergy_bus bus = {&model, read8, write8, delay};
	struct silergy_rail ldo2 = {
		.bus = &bus,
		.type = SILERGY_SY70209,
		.address = 6,
		.min_uv = 3300000,
		.max_uv = 3300000,
		.ramp_uv_per_us = 1000,
		.enable_delay_us = 2000,
		.always_on = true,
	};
	struct silergy_rail ldo3 = ldo2;
	int32_t uv;

	ldo3.min_uv = 800000;
	ldo3.max_uv = 800000;
	model.registers[6][0x58] = 0x88;
	model.registers[6][0x3e] = 0x0c;
	/* LDO2 is 2.8 V, LDO3 is 0.6 V; preserve both unrelated high bits. */
	model.registers[6][0x12] = 0xc4;
	zassert_ok(silergy_rail_init(&ldo2, SY70209_LDO(2)));
	zassert_ok(silergy_rail_init(&ldo3, SY70209_LDO(3)));
	zassert_ok(silergy_voltage_ops.get_level(&ldo2, &uv));
	zassert_equal(uv, 2800000);
	zassert_ok(silergy_voltage_ops.get_level(&ldo3, &uv));
	zassert_equal(uv, 600000);
	zassert_ok(silergy_voltage_ops.set_level(&ldo3, 800000));
	zassert_equal(model.registers[6][0x12], 0xdc);
	zassert_ok(silergy_voltage_ops.get_level(&ldo2, &uv));
	zassert_equal(uv, 2800000);
	zassert_ok(silergy_voltage_ops.set_level(&ldo2, 3300000));
	zassert_equal(model.registers[6][0x12], 0xdf);
	zassert_ok(silergy_voltage_ops.get_level(&ldo3, &uv));
	zassert_equal(uv, 800000);
	zassert_equal(silergy_voltage_ops.set_level(&ldo2, 1800000), -ERANGE);
	zassert_equal(silergy_voltage_ops.set_enabled(&ldo3, false), -EACCES);
	zassert_equal(silergy_rail_init(&ldo2, SY70209_LDO(6)), -EINVAL);
	ldo2.min_uv = 1800000;
	zassert_equal(silergy_rail_init(&ldo2, SY70209_LDO(2)), -ENOTSUP);
}

ZTEST(regulator_silergy, test_peripheral_buck_voltage_ranges)
{
	struct device_model model = {0};
	const struct silergy_bus bus = {&model, read8, write8, delay};
	const int32_t nominal[] = {1800000, 3300000, 800000, 600000};
	const uint8_t selector[] = {16, 127, 7, 24};
	struct silergy_rail rail = {
		.bus = &bus,
		.type = SILERGY_SY70209,
		.address = 6,
		.ramp_uv_per_us = 1000,
		.enable_delay_us = 2000,
		.always_on = true,
	};
	int32_t uv;

	model.registers[6][0x58] = 0x88;
	model.registers[6][0x3d] = 0xff;
	for (unsigned int i = 0; i < 4; i++) {
		rail.min_uv = nominal[i];
		rail.max_uv = nominal[i];
		model.registers[6][9 + i] = 0x80;
		zassert_ok(silergy_rail_init(&rail, SY70209_BUCK(i + 1)));
		zassert_ok(silergy_voltage_ops.set_level(&rail, nominal[i]));
		zassert_equal(model.registers[6][9 + i], 0x80 | selector[i]);
		zassert_ok(silergy_voltage_ops.get_level(&rail, &uv));
		zassert_equal(uv, nominal[i]);
	}
}

ZTEST_SUITE(regulator_silergy, NULL, NULL, NULL, NULL, NULL);
