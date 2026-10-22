/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/performance.h>
#include <zephyr/mgmt/scmi/voltage.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static uint32_t actual;
static unsigned int writes;
static int failure;

static int get(void *ctx, uint32_t *khz)
{
	ARG_UNUSED(ctx);
	*khz = actual;
	return failure;
}

static int set(void *ctx, uint32_t khz)
{
	ARG_UNUSED(ctx);
	writes++;
	if (!failure) {
		actual = khz;
	}
	return failure;
}

static const struct scmi_performance_level levels[] = {
	{500000, 1, 1000},
	{1200000, 3, 1000},
	{1896000, 5, 1000},
	{2298000, 6, 1000},
};
static const struct scmi_performance_domain domain = {
	.name = "c920",
	.levels = levels,
	.count = ARRAY_SIZE(levels),
	.sustained_khz = 1896000,
	.get_level = get,
	.set_level = set,
};
static const struct scmi_server server = {
	.identity = {"Zephyr", "test", 1},
	.performance_domains = &domain,
	.performance_count = 1,
	.voltage_count = 1,
};

static size_t call(uint8_t cmd, uint32_t id, uint32_t arg, size_t len, uint8_t *out, size_t cap)
{
	uint8_t req[16] = {0};

	scmi_put_le32(req, (0x155U << 18) | (SCMI_PERFORMANCE_PROTOCOL << 10) | cmd);
	scmi_put_le32(req + 4, id);
	scmi_put_le32(req + 8, arg);
	return scmi_dispatch(&server, req, len, out, cap);
}

ZTEST(scmi_performance, test_discovery_and_pagination)
{
	uint8_t out[104], req[8] = {0};

	scmi_put_le32(req, (SCMI_BASE_PROTOCOL << 10) | 6);
	zassert_equal(scmi_dispatch(&server, req, 8, out, sizeof(out)), 16);
	zassert_equal(scmi_get_le32(out + 8), 2);
	zassert_equal(out[12], SCMI_PERFORMANCE_PROTOCOL);
	zassert_equal(out[13], SCMI_VOLTAGE_PROTOCOL);
	scmi_put_le32(req + 4, 1);
	zassert_equal(scmi_dispatch(&server, req, 8, out, sizeof(out)), 16);
	zassert_equal(scmi_get_le32(out + 8), 1);
	zassert_equal(out[12], SCMI_VOLTAGE_PROTOCOL);
	zassert_equal(call(3, 0, 0, 8, out, sizeof(out)), 40);
	zassert_equal(scmi_get_le32(out + 8), BIT(30));
	zassert_equal(scmi_get_le32(out + 16), 1896000);
	zassert_equal(call(4, 0, 0, 12, out, 36), 36);
	zassert_equal(scmi_get_le32(out + 8), (2U << 16) | 2U);
	zassert_equal(scmi_get_le32(out + 12), 500000);
	zassert_equal(scmi_get_le32(out + 24), 1200000);
	zassert_equal(call(4, 0, 2, 12, out, 36), 36);
	zassert_equal(scmi_get_le32(out + 8), 2);
	zassert_equal(scmi_get_le32(out + 24), 2298000);
	zassert_equal(call(4, 0, 4, 12, out, sizeof(out)), 8);
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_OUT_OF_RANGE);
}

ZTEST(scmi_performance, test_requests_and_failures)
{
	uint8_t out[104];

	writes = 0;
	failure = 0;
	actual = 500000;
	zassert_equal(call(7, 0, 1200000, 12, out, 7), 0);
	zassert_equal(call(7, 0, 1200000, 8, out, sizeof(out)), 8);
	zassert_equal(writes, 0);
	zassert_equal(call(7, 0, 1200001, 12, out, sizeof(out)), 8);
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_OUT_OF_RANGE);
	zassert_equal(writes, 0);
	call(7, 0, 1200000, 12, out, sizeof(out));
	zassert_equal(scmi_get_le32(out + 4), 0);
	zassert_equal(actual, 1200000);
	failure = -ETIMEDOUT;
	call(7, 0, 1896000, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_COMMS_ERROR);
	zassert_equal(actual, 1200000);
	failure = 0;
	call(8, 0, 0, 8, out, sizeof(out));
	zassert_equal(scmi_get_le32(out + 8), actual);
	call(3, 1, 0, 8, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_FOUND);
	call(2, 5, 0, 8, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_FOUND);
}
struct read_only_state {
	bool enabled;
	int error;
};
static int read_only_enabled(void *context, bool *enabled)
{
	struct read_only_state *state = context;

	*enabled = state->enabled;
	return state->error;
}
static int unexpected_enable(void *context, bool enabled)
{
	ARG_UNUSED(context);
	ARG_UNUSED(enabled);
	zassert_unreachable("read-only domain must not write hardware");
	return -EIO;
}
ZTEST(scmi_performance, test_performance_owned_voltage_is_read_only)
{
	struct read_only_state state = {.enabled = true};
	const struct scmi_voltage_ops voltage_ops = {
		.get_enabled = read_only_enabled,
		.set_enabled = unexpected_enable,
	};
	const struct scmi_voltage_domain voltage = {
		.name = "cpu",
		.min_uv = 800000,
		.max_uv = 1000000,
		.step_uv = 10000,
		.ops = &voltage_ops,
		.read_only = true,
		.context = &state,
	};
	const struct scmi_server managed = {
		.voltage_domains = &voltage,
		.voltage_count = 1,
	};
	uint8_t request[16] = {0}, out[32];

	scmi_put_le32(request, (SCMI_VOLTAGE_PROTOCOL << 10) | 7);
	scmi_put_le32(request + 12, 800000);
	zassert_equal(scmi_dispatch(&managed, request, 16, out, sizeof(out)), 8);
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_DENIED);
	scmi_put_le32(request, (SCMI_VOLTAGE_PROTOCOL << 10) | 5);
	zassert_equal(scmi_dispatch(&managed, request, 12, out, sizeof(out)), 8);
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_DENIED);
	scmi_put_le32(request + 8, 7);
	zassert_equal(scmi_dispatch(&managed, request, 12, out, sizeof(out)), 8);
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_SUCCESS);
	state.error = -EIO;
	scmi_dispatch(&managed, request, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_HARDWARE_ERROR);
	state.error = 0;
	state.enabled = false;
	scmi_dispatch(&managed, request, 12, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_DENIED);
}
static uint32_t memory_read(void *ctx, size_t offset)
{
	zassert_true(offset < SCMI_SMT_SIZE && !(offset & 3));
	return ((uint32_t *)ctx)[offset / 4];
}
static void memory_write(void *ctx, size_t offset, uint32_t value)
{
	zassert_true(offset < SCMI_SMT_SIZE && !(offset & 3));
	((uint32_t *)ctx)[offset / 4] = value;
}
static void memory_barrier(void *ctx)
{
	ARG_UNUSED(ctx);
}
ZTEST(scmi_performance, test_smt_response_capacity_and_multiple_pages)
{
	struct scmi_performance_level many[16];
	struct scmi_performance_domain paged = domain;
	struct scmi_server platform = server;
	uint32_t memory[SCMI_SMT_SIZE / 4] = {0};
	struct scmi_smt_io io = {
		.context = memory, .read32 = memory_read, .write32 = memory_write,
		.barrier = memory_barrier,
	};

	for (size_t i = 0; i < ARRAY_SIZE(many); i++) {
		many[i] = (struct scmi_performance_level){100000 + i * 100000, i + 1, 1000};
	}
	paged.levels = many;
	paged.count = ARRAY_SIZE(many);
	platform.performance_domains = &paged;
	scmi_smt_init(&io);
	memory[1] = 0;
	memory[5] = 8;
	memory[6] = (SCMI_PERFORMANCE_PROTOCOL << 10) | 3;
	zassert_true(scmi_smt_respond(&io, &platform));
	zassert_equal(memory[1], SCMI_CHANNEL_FREE);
	zassert_equal(memory[5], 40);
	for (unsigned int first = 0; first < 16; first += 7) {
		unsigned int count = MIN(7, 16 - first);

		memory[1] = 0;
		memory[5] = 12;
		memory[6] = (SCMI_PERFORMANCE_PROTOCOL << 10) | 4;
		memory[7] = 0;
		memory[8] = first;
		zassert_true(scmi_smt_respond(&io, &platform));
		zassert_equal(memory[1], SCMI_CHANNEL_FREE);
		zassert_equal(memory[5], 12 + count * 12);
		zassert_equal(memory[8], count | ((16 - first - count) << 16));
		zassert_equal(memory[9], many[first].khz);
	}
}
ZTEST_SUITE(scmi_performance, NULL, NULL, NULL, NULL, NULL);
