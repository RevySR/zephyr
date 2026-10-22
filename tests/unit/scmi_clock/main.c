/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/clock.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static const uint64_t rates[] = {24000000, 165000000, 5000000000ULL};
static uint64_t actual;
static bool enabled;
static int failure;
static unsigned int writes;

static int describe(void *ctx, unsigned int index, uint64_t *rate)
{
	ARG_UNUSED(ctx);
	*rate = rates[index];
	return failure;
}

static int get_rate(void *ctx, uint64_t *rate)
{
	ARG_UNUSED(ctx);
	*rate = actual;
	return failure;
}

static int set_rate(void *ctx, uint64_t rate)
{
	ARG_UNUSED(ctx);
	writes++;
	if (!failure) {
		actual = rate;
	}
	return failure;
}

static int get_enabled(void *ctx, bool *state)
{
	ARG_UNUSED(ctx);
	*state = enabled;
	return failure;
}

static int set_enabled(void *ctx, bool state)
{
	ARG_UNUSED(ctx);
	writes++;
	if (!failure) {
		enabled = state;
	}
	return failure;
}

static const struct scmi_clock_ops ops = {
	.describe_rate = describe,
	.get_rate = get_rate,
	.set_rate = set_rate,
	.get_enabled = get_enabled,
	.set_enabled = set_enabled,
};
static const struct scmi_clock clock = {
	.name = "peripheral",
	.ops = &ops,
	.rate_count = ARRAY_SIZE(rates),
	.enable_latency_us = 12,
};
static const struct scmi_server server = {.clocks = &clock, .clock_count = 1};

static size_t call(uint32_t cmd, uint32_t arg0, uint32_t arg1, uint64_t rate, size_t size,
		   uint8_t *response, size_t capacity)
{
	uint8_t request[20];

	scmi_put_le32(request, (0x155U << 18) | (SCMI_CLOCK_PROTOCOL << 10) | cmd);
	scmi_put_le32(request + 4, arg0);
	scmi_put_le32(request + 8, arg1);
	scmi_put_le32(request + 12, rate);
	scmi_put_le32(request + 16, rate >> 32);
	return scmi_dispatch(&server, request, size, response, capacity);
}

static int32_t status(const uint8_t *response)
{
	return (int32_t)scmi_get_le32(response + 4);
}

ZTEST(scmi_clock, test_discovery_pagination_and_64bit_rates)
{
	uint8_t out[104], request[8] = {0};

	failure = 0;
	enabled = true;
	scmi_put_le32(request, (SCMI_BASE_PROTOCOL << 10) | 6);
	zassert_equal(scmi_dispatch(&server, request, 8, out, sizeof(out)), 16);
	zassert_equal(out[12], SCMI_CLOCK_PROTOCOL);
	zassert_equal(call(0, 0, 0, 0, 4, out, sizeof(out)), 12);
	zassert_equal(scmi_get_le32(out + 8), 0x20000);
	zassert_equal(call(3, 0, 0, 0, 8, out, sizeof(out)), 32);
	zassert_equal(scmi_get_le32(out + 8), 1);
	zassert_mem_equal(out + 12, "peripheral", 11);
	zassert_equal(scmi_get_le32(out + 28), 12);
	zassert_equal(call(4, 0, 0, 0, 12, out, 28), 28);
	zassert_equal(scmi_get_le32(out + 8), (1U << 16) | 2);
	zassert_equal(scmi_get_le32(out + 12), rates[0]);
	zassert_equal(scmi_get_le32(out + 20), rates[1]);
	zassert_equal(call(4, 0, 2, 0, 12, out, sizeof(out)), 20);
	zassert_equal(scmi_get_le32(out + 8), 1);
	zassert_equal(((uint64_t)scmi_get_le32(out + 16) << 32) | scmi_get_le32(out + 12),
		      rates[2]);
	call(4, 0, 3, 0, 12, out, sizeof(out));
	zassert_equal(status(out), SCMI_OUT_OF_RANGE);
	actual = rates[2];
	zassert_equal(call(6, 0, 0, 0, 8, out, sizeof(out)), 16);
	zassert_equal(((uint64_t)scmi_get_le32(out + 12) << 32) | scmi_get_le32(out + 8), actual);
}

ZTEST(scmi_clock, test_rounding_and_error_propagation)
{
	uint8_t out[104];

	failure = 0;
	call(5, 0, 0, 100000000, 20, out, sizeof(out));
	zassert_equal(status(out), 0);
	zassert_equal(actual, rates[0]);
	call(5, BIT(2), 0, 100000000, 20, out, sizeof(out));
	zassert_equal(actual, rates[1]);
	call(5, BIT(3), 0, 4000000000ULL, 20, out, sizeof(out));
	zassert_equal(actual, rates[2]);
	call(5, 0, 0, 0, 20, out, sizeof(out));
	zassert_equal(status(out), SCMI_INVALID_PARAMETERS);
	call(5, BIT(2), 0, UINT64_MAX, 20, out, sizeof(out));
	zassert_equal(status(out), SCMI_INVALID_PARAMETERS);
	writes = 0;
	call(5, BIT(0), 0, rates[0], 20, out, sizeof(out));
	zassert_equal(status(out), SCMI_NOT_SUPPORTED);
	call(5, BIT(4), 0, rates[0], 20, out, sizeof(out));
	zassert_equal(status(out), SCMI_INVALID_PARAMETERS);
	zassert_equal(writes, 0);
	failure = -ETIMEDOUT;
	call(7, 0, 1, 0, 12, out, sizeof(out));
	zassert_equal(status(out), SCMI_COMMS_ERROR);
	failure = -EACCES;
	call(7, 0, 1, 0, 12, out, sizeof(out));
	zassert_equal(status(out), SCMI_DENIED);
	failure = 0;
}

ZTEST(scmi_clock, test_invalid_requests_do_not_write)
{
	uint8_t out[104];

	writes = 0;
	failure = 0;
	for (size_t i = 0; i < 20; i++) {
		call(5, 0, 0, rates[1], i, out, sizeof(out));
	}
	for (size_t i = 0; i < 8; i++) {
		memset(out, 0xaa, sizeof(out));
		zassert_equal(call(5, 0, 0, rates[1], 20, out, i), 0);
		zassert_equal(out[0], 0xaa);
	}
	call(5, 0, UINT32_MAX, rates[1], 20, out, sizeof(out));
	zassert_equal(status(out), SCMI_NOT_FOUND);
	call(7, 0, 2, 0, 12, out, sizeof(out));
	zassert_equal(status(out), SCMI_INVALID_PARAMETERS);
	zassert_equal(writes, 0);
	call(7, 0, 1, 0, 12, out, sizeof(out));
	zassert_equal(status(out), 0);
	zassert_true(enabled);
	call(7, 0, 0, 0, 12, out, sizeof(out));
	zassert_equal(status(out), 0);
	zassert_false(enabled);
}

static unsigned int lookups;

static int lookup(void *context, uint32_t id, struct scmi_clock *resource, char name[16])
{
	zassert_equal(context, &clock);
	zassert_true(id < 2);
	lookups++;
	*resource = clock;
	strcpy(name, "dynamic-clock");
	resource->name = name;
	return failure;
}

ZTEST(scmi_clock, test_dynamic_resources_validate_before_lookup)
{
	struct scmi_server dynamic = {
		.clock_count = 2,
		.clock_lookup = lookup,
		.resource_context = (void *)&clock,
	};
	uint8_t request[12], out[64];

	failure = 0;
	lookups = writes = 0;
	scmi_put_le32(request, (SCMI_CLOCK_PROTOCOL << 10) | 3);
	scmi_put_le32(request + 4, 1);
	zassert_equal(scmi_dispatch(&dynamic, request, 8, out, sizeof(out)), 32);
	zassert_mem_equal(out + 12, "dynamic-clock", 14);
	zassert_equal(lookups, 1);
	scmi_put_le32(request + 4, 2);
	scmi_dispatch(&dynamic, request, 8, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_FOUND);
	zassert_equal(lookups, 1);
	scmi_put_le32(request, (SCMI_CLOCK_PROTOCOL << 10) | 7);
	scmi_put_le32(request + 4, 1);
	scmi_put_le32(request + 8, 1);
	failure = -EIO;
	scmi_dispatch(&dynamic, request, sizeof(request), out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_HARDWARE_ERROR);
	zassert_equal(writes, 0);
	failure = 0;
}

ZTEST_SUITE(scmi_clock, NULL, NULL, NULL, NULL, NULL);
