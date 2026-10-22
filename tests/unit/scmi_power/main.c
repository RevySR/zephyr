/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/power.h>
#include <zephyr/ztest.h>
#include <errno.h>

static bool powered;
static int failure;
static unsigned int writes;

static int set(void *ctx, bool on)
{
	ARG_UNUSED(ctx);
	writes++;
	if (!failure) {
		powered = on;
	}
	return failure;
}

static int get(void *ctx, bool *on)
{
	ARG_UNUSED(ctx);
	*on = powered;
	return failure;
}

static const struct scmi_power_domain domain = {.name = "gpu", .set_on = set, .get_on = get};
static const struct scmi_server server = {
	.power_domains = &domain,
	.power_count = 1,
	.clock_count = 1,
	.reset_count = 1,
	.voltage_count = 1,
	.performance_count = 1,
};

static size_t call(uint32_t cmd, uint32_t a, uint32_t b, uint32_t c, size_t len, uint8_t *out,
		   size_t capacity)
{
	uint8_t request[16];

	scmi_put_le32(request, (SCMI_POWER_PROTOCOL << 10) | cmd);
	scmi_put_le32(request + 4, a);
	scmi_put_le32(request + 8, b);
	scmi_put_le32(request + 12, c);
	return scmi_dispatch(&server, request, len, out, capacity);
}

ZTEST(scmi_power, test_discovery_and_state)
{
	uint8_t out[32], request[8] = {0};

	failure = 0;
	scmi_put_le32(request, (SCMI_BASE_PROTOCOL << 10) | 6);
	zassert_equal(scmi_dispatch(&server, request, 8, out, sizeof(out)), 20);
	zassert_equal(scmi_get_le32(out + 8), 5);
	zassert_equal(out[12], SCMI_POWER_PROTOCOL);
	zassert_equal(out[16], SCMI_VOLTAGE_PROTOCOL);
	zassert_equal(scmi_dispatch(&server, request, 8, out, 16), 0);
	zassert_equal(call(1, 0, 0, 0, 4, out, sizeof(out)), 24);
	zassert_equal(scmi_get_le32(out + 8), 1);
	zassert_equal(scmi_get_le32(out + 12), 0);
	zassert_equal(call(3, 0, 0, 0, 8, out, sizeof(out)), 28);
	zassert_equal(scmi_get_le32(out + 8), BIT(29));
	call(4, 0, 0, 0, 16, out, sizeof(out));
	zassert_true(powered);
	call(5, 0, 0, 0, 8, out, sizeof(out));
	zassert_equal(scmi_get_le32(out + 8), 0);
	call(4, 0, 0, BIT(30), 16, out, sizeof(out));
	zassert_false(powered);
}

ZTEST(scmi_power, test_invalid_requests_and_rejected_transition)
{
	uint8_t out[32];

	failure = 0;
	writes = 0;
	for (size_t i = 0; i < 16; i++) {
		call(4, 0, 0, 0, i, out, sizeof(out));
	}
	call(4, 1, 0, 0, 16, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_SUPPORTED);
	call(4, 0, 0, 1, 16, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_INVALID_PARAMETERS);
	zassert_equal(call(4, 0, 0, 0, 16, out, 7), 0);
	zassert_equal(writes, 0);
	powered = false;
	failure = -ETIMEDOUT;
	call(4, 0, 0, 0, 16, out, sizeof(out));
	zassert_false(powered);
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_COMMS_ERROR);
}

ZTEST_SUITE(scmi_power, NULL, NULL, NULL, NULL, NULL);
