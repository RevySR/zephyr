/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/client.h>
#include <errno.h>

static int wait_free(struct scmi_client *c)
{
	uint32_t start = c->ticks(c->io.context);

	for (uint32_t i = 0; i < c->poll_limit; i++) {
		if (c->io.read32(c->io.context, 4) & SCMI_CHANNEL_FREE) {
			return 0;
		}
		if ((uint32_t)(c->ticks(c->io.context) - start) >= c->timeout_ticks) {
			break;
		}
	}
	c->poisoned = true;
	return -ETIMEDOUT;
}

int scmi_client_call(struct scmi_client *c, uint8_t protocol, uint8_t command, const void *request,
		     size_t request_size, void *response, size_t response_capacity,
		     size_t *response_size, int32_t *remote_status)
{
	uint32_t header, length, status;
	int ret;

	if (!c || !response_size || !remote_status || !c->ticks || !c->io.read32 ||
	    !c->io.write32 || !c->io.barrier || !c->io.notify || !c->poll_limit ||
	    !c->timeout_ticks || c->timeout_ticks > INT32_MAX ||
	    request_size > SCMI_MESSAGE_MAX - 4 || (request_size & 3) ||
	    (request_size && !request) || (response_capacity && !response)) {
		return -EINVAL;
	}
	if (c->busy) {
		return -EBUSY;
	}
	if (c->poisoned) {
		return -EIO;
	}
	*response_size = 0;
	c->busy = true;
	ret = wait_free(c);
	if (ret) {
		goto out;
	}
	header = (uint32_t)(c->next_token++ & 0x3ffU) << 18 | (uint32_t)protocol << 10 | command;
	c->io.write32(c->io.context, 4, 0);
	c->io.write32(c->io.context, 16, 0); /* completion is polled */
	c->io.write32(c->io.context, 20, (uint32_t)request_size + 4);
	c->io.write32(c->io.context, 24, header);
	for (size_t i = 0; i < request_size; i += 4) {
		c->io.write32(c->io.context, 28 + i, scmi_get_le32((const uint8_t *)request + i));
	}
	c->io.barrier(c->io.context);
	c->io.notify(c->io.context);
	ret = wait_free(c);
	if (ret) {
		goto out;
	}
	c->io.barrier(c->io.context);
	status = c->io.read32(c->io.context, 4);
	length = c->io.read32(c->io.context, 20);
	if (status != SCMI_CHANNEL_FREE || length < 8 || length > SCMI_MESSAGE_MAX ||
	    (length & 3) || c->io.read32(c->io.context, 24) != header) {
		c->poisoned = true;
		ret = -EPROTO;
		goto out;
	}
	if (length - 8 > response_capacity) {
		ret = -EMSGSIZE;
		goto out;
	}
	*remote_status = (int32_t)c->io.read32(c->io.context, 28);
	for (size_t i = 0; i < length - 8; i += 4) {
		scmi_put_le32((uint8_t *)response + i, c->io.read32(c->io.context, 32 + i));
	}
	*response_size = length - 8;
out:
	c->busy = false;
	return ret;
}
