/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/server.h>
#include <zephyr/ztest.h>
#define assert(condition) zassert_true(condition, "assertion failed")
#include <stdio.h>
#include <string.h>

static const struct scmi_server server = {
	.identity = {"aon-firmware", "test", 1},
};

static void test_base(void)
{
	/* Token 0x155, Base 0x10, command 0; explicit little-endian wire data. */
	uint8_t request[12] = {0x00, 0x40, 0x54, 0x05};
	uint8_t out[40];
	const uint8_t version[] = {0x00, 0x40, 0x54, 0x05, 0, 0, 0, 0, 0, 0, 2, 0};
	const unsigned int sizes[] = {12, 12, 12, 24, 24, 12, 12, 28};

	memset(out, 0xa5, sizeof(out));
	assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 12);
	assert(memcmp(out, version, sizeof(version)) == 0);
	assert(out[12] == 0xa5);
	for (unsigned int cmd = 0; cmd < 8; cmd++) {
		bool has_argument = cmd == 2 || cmd == 6 || cmd == 7;
		size_t len = has_argument ? 8 : 4;

		request[0] = (uint8_t)cmd;
		scmi_put_le32(request + 4, 0);
		assert(scmi_dispatch(&server, request, len, out, sizeof(out)) == sizes[cmd]);
		assert(scmi_get_le32(out + 4) == 0);
		assert(memcmp(request, out, 4) == 0);
		assert(scmi_dispatch(&server, request, len + 4, out, sizeof(out)) == 8);
		assert((int32_t)scmi_get_le32(out + 4) == -2);
		if (has_argument) {
			assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 8);
			assert((int32_t)scmi_get_le32(out + 4) == -2);
		}
	}
	request[0] = 1;
	assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 12);
	assert(scmi_get_le32(out + 8) == 0x100); /* Base excluded from protocol count */
	request[0] = 3;
	assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 24);
	assert(memcmp(out + 8, "aon-firmware\0\0\0\0\0\0\0\0", 16) == 0);
	request[0] = 7;
	scmi_put_le32(request + 4, UINT32_MAX);
	assert(scmi_dispatch(&server, request, 8, out, sizeof(out)) == 28);
	assert(scmi_get_le32(out + 8) == 1);
	assert(strcmp((char *)out + 12, "ap") == 0);
	scmi_put_le32(request + 4, 2);
	assert(scmi_dispatch(&server, request, 8, out, sizeof(out)) == 8);
	assert((int32_t)scmi_get_le32(out + 4) == -4);
	request[0] = 2;
	scmi_put_le32(request + 4, 256); /* Do not truncate command to eight bits. */
	assert(scmi_dispatch(&server, request, 8, out, sizeof(out)) == 8);
	assert((int32_t)scmi_get_le32(out + 4) == -4);
	request[0] = 0;
	request[1] = 0x5c; /* Voltage protocol is not advertised or implemented yet. */
	assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 8);
	assert((int32_t)scmi_get_le32(out + 4) == -1);
	request[1] = 0x41; /* An incoming delayed response is not a command. */
	assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 8);
	assert((int32_t)scmi_get_le32(out + 4) == -10);
	request[1] = 0x40;
	request[3] |= 0x80; /* Reserved header bits. */
	assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 8);
	assert((int32_t)scmi_get_le32(out + 4) == -10);
	request[3] &= 0x0f;
	memset(out, 0xa5, sizeof(out));
	assert(scmi_dispatch(&server, request, 4, out, 11) == 0);
	assert(out[0] == 0xa5);
	for (size_t n = 0; n < 4; n++) {
		assert(scmi_dispatch(&server, request, n, out, sizeof(out)) == 0);
	}
	for (unsigned int token = 0; token < 1024; token++) {
		scmi_put_le32(request, (token << 18) | 0x4000);
		assert(scmi_dispatch(&server, request, 4, out, sizeof(out)) == 12);
		assert(memcmp(request, out, 4) == 0);
	}
}

struct memory {
	uint32_t words[SCMI_SMT_SIZE / 4];
	unsigned int reads, writes, notifications;
	unsigned int sequence, last_barrier, last_write, free_write;
};

static uint32_t read_word(void *context, size_t offset)
{
	struct memory *m = context;

	assert(offset % 4 == 0 && offset < SCMI_SMT_SIZE);
	m->reads++;
	return m->words[offset / 4];
}

static void write_word(void *context, size_t offset, uint32_t value)
{
	struct memory *m = context;

	assert(offset % 4 == 0 && offset < SCMI_SMT_SIZE);
	if (offset == 4 && (value & SCMI_CHANNEL_FREE)) {
		assert(m->last_barrier > m->last_write);
		m->free_write = m->sequence + 1;
	}
	m->last_write = ++m->sequence;
	m->words[offset / 4] = value;
	m->writes++;
}

static void barrier(void *context)
{
	struct memory *m = context;

	m->last_barrier = ++m->sequence;
}

static void notify(void *context)
{
	struct memory *m = context;

	assert(m->words[1] & SCMI_CHANNEL_FREE);
	assert(m->last_barrier > m->free_write);
	m->notifications++;
}

static void submit(struct memory *m, uint32_t length, uint32_t flags)
{
	m->words[1] = 0;
	m->words[4] = flags;
	m->words[5] = length;
	m->words[6] = 0x05544000;
}

static void test_transport(void)
{
	struct memory m = {0};
	const struct scmi_smt_io io = {&m, read_word, write_word, barrier, notify};
	const unsigned int bad_lengths[] = {0, 1, 3, 5, 233, 256, UINT32_MAX};

	scmi_smt_init(&io);
	assert(m.words[1] == 1 && m.notifications == 0);
	unsigned int writes = m.writes;

	assert(!scmi_smt_respond(&io, &server));
	assert(m.writes == writes); /* Spurious doorbell must not overwrite a response. */
	submit(&m, 4, 0);
	assert(scmi_smt_respond(&io, &server));
	assert(m.words[1] == 1 && m.words[5] == 12);
	assert(m.words[6] == 0x05544000 && m.words[7] == 0 && m.words[8] == 0x20000);
	assert(m.notifications == 0);
	submit(&m, 4, 1);
	assert(scmi_smt_respond(&io, &server));
	assert(m.notifications == 1);
	for (size_t i = 0; i < ARRAY_SIZE(bad_lengths); i++) {
		submit(&m, bad_lengths[i], 0);
		assert(scmi_smt_respond(&io, &server));
		assert(m.words[1] == (SCMI_CHANNEL_FREE | SCMI_CHANNEL_ERROR));
	}
	submit(&m, SCMI_MESSAGE_MAX, 0); /* In bounds, but invalid Base command length. */
	assert(scmi_smt_respond(&io, &server));
	assert(m.words[1] == 1 && (int32_t)m.words[7] == -2);
	submit(&m, 4, 2); /* Reserved transport flags. */
	assert(scmi_smt_respond(&io, &server));
	assert(m.words[1] == 3);
	submit(&m, 4, 0);
	m.words[1] = SCMI_CHANNEL_ERROR;
	assert(scmi_smt_respond(&io, &server));
	assert(m.words[1] == 3);
}

ZTEST(scmi_server, test_contract)
{
	test_base();
	test_transport();
	puts("PASS: Base wire vectors, 1024 tokens, bounds and SMT publication ordering");
}

ZTEST_SUITE(scmi_server, NULL, NULL, NULL, NULL, NULL);
