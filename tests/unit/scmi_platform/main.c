/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/sensor.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static unsigned int reads, requests;
static int failure;

static int sensor_read(void *context, int64_t *value)
{
	ARG_UNUSED(context);
	reads++;
	*value = -40000;
	return failure;
}

static uint32_t sensor_config;
static unsigned int config_writes;

static int config_get(void *context, uint32_t *config)
{
	ARG_UNUSED(context);
	*config = sensor_config;
	return 0;
}

static int config_set(void *context, uint32_t config)
{
	ARG_UNUSED(context);
	config_writes++;
	sensor_config = config;
	return 0;
}

static int sensor_lookup(void *context, uint32_t id, struct scmi_sensor *sensor, char name[16])
{
	ARG_UNUSED(context);
	ARG_UNUSED(id);
	strcpy(name, "temperature");
	*sensor = (struct scmi_sensor){.name = name,
				       .type = 2,
				       .scale = -3,
				       .read = sensor_read,
				       .get_config = config_get,
				       .set_config = config_set};
	return failure;
}

static int system_request(void *context, uint32_t state)
{
	ARG_UNUSED(context);
	zassert_true(state <= 1);
	requests++;
	return failure;
}

static const struct scmi_server server = {
	.power_count = 1,
	.clock_count = 1,
	.reset_count = 1,
	.voltage_count = 1,
	.performance_count = 1,
	.sensor_count = 6,
	.sensor_lookup = sensor_lookup,
	.system_request = system_request,
};

static size_t call(uint8_t protocol, uint8_t cmd, uint32_t a, uint32_t b, size_t len, uint8_t *out,
		   size_t capacity)
{
	uint8_t req[12];

	scmi_put_le32(req, ((uint32_t)protocol << 10) | cmd);
	scmi_put_le32(req + 4, a);
	scmi_put_le32(req + 8, b);
	return scmi_dispatch(&server, req, len, out, capacity);
}

ZTEST(scmi_platform, test_all_protocols_fit_the_discovery_response)
{
	uint8_t out[104];

	zassert_equal(call(0x10, 6, 0, 0, 8, out, sizeof(out)), 20);
	zassert_equal(scmi_get_le32(out + 8), 7);
	for (unsigned int i = 0; i < 7; i++) {
		zassert_equal(out[12 + i], 0x11 + i);
	}
	zassert_equal(call(0x10, 6, 4, 0, 8, out, sizeof(out)), 16);
	zassert_equal(scmi_get_le32(out + 8), 3);
	zassert_equal(out[12], 0x15);
}

ZTEST(scmi_platform, test_sensor_pagination_and_signed_reading)
{
	uint8_t out[104];

	failure = 0;
	zassert_equal(call(0x15, 3, 0, 0, 8, out, sizeof(out)), 96);
	zassert_equal(scmi_get_le32(out + 8), (3U << 16) | 3);
	zassert_equal(scmi_get_le32(out + 12), 0);
	zassert_equal(scmi_get_le32(out + 20), (29U << 11) | 2);
	zassert_equal(scmi_get_le32(out + 68), 2);
	zassert_equal(call(0x15, 3, 5, 0, 8, out, sizeof(out)), 40);
	zassert_equal(scmi_get_le32(out + 8), 1);
	zassert_equal(call(0x15, 6, 0, 0, 12, out, sizeof(out)), 16);
	zassert_equal(scmi_get_le32(out + 8), (uint32_t)-40000);
	zassert_equal(scmi_get_le32(out + 12), UINT32_MAX);
	reads = 0;
	call(0x15, 6, 0, 1, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_SUPPORTED);
	zassert_equal(call(0x15, 6, 0, 0, 12, out, 8), 0);
	zassert_equal(reads, 0);
}

ZTEST(scmi_platform, test_system_power_guards)
{
	uint8_t out[104];

	failure = 0;
	requests = 0;
	for (unsigned int state = 2; state < 6; state++) {
		call(0x12, 3, 0, state, 12, out, sizeof(out));
		zassert_not_equal(scmi_get_le32(out + 4), 0);
	}
	call(0x12, 3, 1, 0, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_SUPPORTED);
	for (size_t len = 0; len < 12; len++) {
		call(0x12, 3, 0, 0, len, out, sizeof(out));
	}
	zassert_equal(call(0x12, 3, 0, 0, 12, out, 7), 0);
	zassert_equal(requests, 0);
	call(0x12, 3, 0, 1, 12, out, sizeof(out));
	zassert_equal(scmi_get_le32(out + 4), 0);
	zassert_equal(requests, 1);
	failure = -EIO;
	call(0x12, 3, 0, 0, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_HARDWARE_ERROR);
	failure = 0;
}

ZTEST(scmi_platform, test_sensor_configuration_dispatch)
{
	uint8_t out[104];

	failure = 0;
	sensor_config = config_writes = 0;
	zassert_equal(call(0x15, 10, 0, 1, 12, out, sizeof(out)), 8);
	zassert_equal(scmi_get_le32(out + 4), 0);
	zassert_equal(config_writes, 1);
	zassert_equal(call(0x15, 9, 0, 0, 8, out, sizeof(out)), 12);
	zassert_equal(scmi_get_le32(out + 8), 1);
	call(0x15, 10, 6, 0, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_FOUND);
	call(0x15, 10, 0, 0, 8, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_INVALID_PARAMETERS);
	zassert_equal(config_writes, 1);
	failure = -EIO;
	call(0x15, 10, 0, 0, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_HARDWARE_ERROR);
	zassert_equal(config_writes, 1);
	failure = 0;
}

ZTEST_SUITE(scmi_platform, NULL, NULL, NULL, NULL, NULL);
