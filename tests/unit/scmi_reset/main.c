/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/reset.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static unsigned int writes;
static bool asserted;
static int failure;

static int set(void *ctx, bool state)
{
	ARG_UNUSED(ctx);
	writes++;
	if (!failure) {
		asserted = state;
	}
	return failure;
}

static int pulse(void *ctx)
{
	int ret = set(ctx, true);

	return ret ? ret : set(ctx, false);
}

static const struct scmi_reset_domain domain = {
	.name = "uart4",
	.latency_us = 10,
	.set_asserted = set,
	.reset = pulse,
};
static const struct scmi_server server = {.reset_domains = &domain, .reset_count = 1};

static size_t call(uint32_t cmd, uint32_t id, uint32_t flags, uint32_t state, size_t size,
		   uint8_t *out, size_t capacity)
{
	uint8_t request[16];

	scmi_put_le32(request, (SCMI_RESET_PROTOCOL << 10) | cmd);
	scmi_put_le32(request + 4, id);
	scmi_put_le32(request + 8, flags);
	scmi_put_le32(request + 12, state);
	return scmi_dispatch(&server, request, size, out, capacity);
}

ZTEST(scmi_reset, test_discovery_and_sequences)
{
	uint8_t out[32], request[8] = {0};

	writes = 0;
	failure = 0;
	scmi_put_le32(request, (SCMI_BASE_PROTOCOL << 10) | 6);
	zassert_equal(scmi_dispatch(&server, request, 8, out, sizeof(out)), 16);
	zassert_equal(out[12], SCMI_RESET_PROTOCOL);
	zassert_equal(call(0, 0, 0, 0, 4, out, sizeof(out)), 12);
	zassert_equal(scmi_get_le32(out + 8), 0x30000);
	zassert_equal(call(3, 0, 0, 0, 8, out, sizeof(out)), 32);
	zassert_equal(scmi_get_le32(out + 8), 0);
	zassert_equal(scmi_get_le32(out + 12), 10);
	zassert_mem_equal(out + 16, "uart4", 6);
	call(4, 0, BIT(1), 0, 16, out, sizeof(out));
	zassert_true(asserted);
	call(4, 0, 0, 0, 16, out, sizeof(out));
	zassert_false(asserted);
	call(4, 0, BIT(0), 0, 16, out, sizeof(out));
	zassert_equal(scmi_get_le32(out + 4), 0);
	zassert_equal(writes, 4);
	zassert_false(asserted);
}

ZTEST(scmi_reset, test_validation_and_failed_pulse)
{
	uint8_t out[32];

	failure = 0;
	writes = 0;
	for (size_t i = 0; i < 16; i++) {
		call(4, 0, BIT(0), 0, i, out, sizeof(out));
	}
	for (size_t i = 0; i < 8; i++) {
		zassert_equal(call(4, 0, BIT(0), 0, 16, out, i), 0);
	}
	call(4, 0, BIT(0), 1, 16, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_INVALID_PARAMETERS);
	call(4, 0, BIT(2), 0, 16, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_INVALID_PARAMETERS);
	call(4, 0, BIT(2) | BIT(0), 0, 16, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_SUPPORTED);
	call(4, UINT32_MAX, 0, 0, 16, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_FOUND);
	zassert_equal(writes, 0);
	failure = -EIO;
	call(4, 0, BIT(0), 0, 16, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_HARDWARE_ERROR);
	zassert_equal(writes, 1);
	failure = 0;
}

static unsigned int lookups;

static int lookup(void *context, uint32_t id, struct scmi_reset_domain *resource, char name[16])
{
	zassert_equal(context, &domain);
	zassert_true(id < 2);
	lookups++;
	*resource = domain;
	strcpy(name, "dynamic-reset");
	resource->name = name;
	return failure;
}

ZTEST(scmi_reset, test_dynamic_resources_validate_before_lookup)
{
	struct scmi_server dynamic = {
		.reset_count = 2,
		.reset_lookup = lookup,
		.resource_context = (void *)&domain,
	};
	uint8_t request[16], out[32];

	failure = 0;
	lookups = writes = 0;
	scmi_put_le32(request, (SCMI_RESET_PROTOCOL << 10) | 3);
	scmi_put_le32(request + 4, 1);
	zassert_equal(scmi_dispatch(&dynamic, request, 8, out, sizeof(out)), 32);
	zassert_mem_equal(out + 16, "dynamic-reset", 14);
	zassert_equal(lookups, 1);
	scmi_put_le32(request + 4, 2);
	scmi_dispatch(&dynamic, request, 8, out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_NOT_FOUND);
	zassert_equal(lookups, 1);
	scmi_put_le32(request, (SCMI_RESET_PROTOCOL << 10) | 4);
	scmi_put_le32(request + 4, 1);
	scmi_put_le32(request + 8, 0);
	scmi_put_le32(request + 12, 0);
	failure = -EIO;
	scmi_dispatch(&dynamic, request, sizeof(request), out, sizeof(out));
	zassert_equal((int32_t)scmi_get_le32(out + 4), SCMI_HARDWARE_ERROR);
	zassert_equal(writes, 0);
	failure = 0;
}

ZTEST_SUITE(scmi_reset, NULL, NULL, NULL, NULL, NULL);
