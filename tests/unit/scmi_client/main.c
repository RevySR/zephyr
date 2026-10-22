/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/client.h>
#include <zephyr/ztest.h>
#define assert(condition) zassert_true(condition, "assertion failed")
#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct {
	uint32_t words[64], tick, kicks, writes;
	bool lost, corrupt, stuck;
} channel;
static const struct scmi_server server = {.identity = {"aon-firmware", "test", 3}};
static uint32_t rd(void *context, size_t offset)
{
	(void)context;
	assert(offset < 256 && !(offset & 3));
	return channel.words[offset / 4];
}
static void wr(void *context, size_t offset, uint32_t value)
{
	(void)context;
	assert(offset < 256 && !(offset & 3));
	channel.writes++;
	channel.words[offset / 4] = value;
}
static void fence(void *context)
{
	(void)context;
}
static void server_notify(void *context)
{
	(void)context;
	assert(false);
}
static const struct scmi_smt_io io = {NULL, rd, wr, fence, server_notify};
static void kick(void *context)
{
	(void)context;
	channel.kicks++;
	if (channel.lost) {
		return;
	}
	assert(scmi_smt_respond(&io, &server));
	if (channel.corrupt) {
		channel.words[6] ^= 1U << 18;
	}
}
static uint32_t ticks(void *context)
{
	(void)context;
	if (!channel.stuck) {
		channel.tick++;
	}
	return channel.tick;
}
static struct scmi_client make_client(void)
{
	memset(&channel, 0, sizeof(channel));
	scmi_smt_init(&io);
	return (struct scmi_client){.io = {NULL, rd, wr, fence, kick},
				    .ticks = ticks,
				    .timeout_ticks = 10,
				    .poll_limit = 100};
}
ZTEST(scmi_client, test_contract)
{
	struct scmi_client c = make_client();
	uint8_t response[32];
	size_t size;
	int32_t status = 123;

	for (unsigned int i = 0; i < 1025; i++) {
		assert(scmi_client_call(&c, 0x10, 0, NULL, 0, response, sizeof(response), &size,
					&status) == 0);
		assert(status == 0 && size == 4 && scmi_get_le32(response) == 0x20000);
		assert(channel.words[6] >> 18 == (i & 1023));
	}
	assert(scmi_client_call(&c, 0xff, 0, NULL, 0, response, sizeof(response), &size, &status) ==
	       0);
	assert(status == SCMI_NOT_SUPPORTED && size == 0);
	uint8_t self[4] = {0xff, 0xff, 0xff, 0xff};

	assert(scmi_client_call(&c, 0x10, 7, self, 4, response, sizeof(response), &size, &status) ==
	       0);
	assert(status == 0 && size == 20 && scmi_get_le32(response) == 1);
	assert(!strcmp((char *)response + 4, "ap"));
	assert(scmi_client_call(&c, 0x10, 0, NULL, 0, response, 1, &size, &status) == -EMSGSIZE);
	assert(!c.poisoned);
	uint32_t writes = channel.writes;

	assert(scmi_client_call(&c, 0x10, 0, response, SIZE_MAX, response, sizeof(response), &size,
				&status) == -EINVAL);
	assert(channel.writes == writes);
	c = make_client();
	channel.lost = true;
	channel.tick = UINT32_MAX - 3;
	assert(scmi_client_call(&c, 0x10, 0, NULL, 0, response, sizeof(response), &size, &status) ==
	       -ETIMEDOUT);
	assert(c.poisoned && channel.kicks == 1);
	assert(scmi_client_call(&c, 0x10, 0, NULL, 0, response, sizeof(response), &size, &status) ==
	       -EIO);
	assert(channel.kicks == 1);
	c = make_client();
	channel.corrupt = true;
	assert(scmi_client_call(&c, 0x10, 0, NULL, 0, response, sizeof(response), &size, &status) ==
	       -EPROTO);
	assert(c.poisoned);
	c = make_client();
	channel.stuck = true;
	channel.words[1] = 0;
	writes = channel.writes;
	assert(scmi_client_call(&c, 0x10, 0, NULL, 0, response, sizeof(response), &size, &status) ==
	       -ETIMEDOUT);
	assert(channel.writes == writes && channel.kicks == 0);
	puts("PASS: AP/client-to-server wire exchange, token wrap, remote errors, timeout and "
	     "stale reply");
}

ZTEST_SUITE(scmi_client, NULL, NULL, NULL, NULL, NULL);
