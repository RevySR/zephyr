/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/server.h>

#define CHANNEL_STATUS    4U
#define FLAGS             16U
#define LENGTH            20U
#define INTERRUPT_ENABLED 1U

void scmi_smt_init(const struct scmi_smt_io *io)
{
	for (size_t offset = 0; offset < SCMI_SMT_SIZE; offset += 4) {
		io->write32(io->context, offset, 0);
	}
	io->barrier(io->context);
	io->write32(io->context, CHANNEL_STATUS, SCMI_CHANNEL_FREE);
	io->barrier(io->context);
}

bool scmi_smt_respond(const struct scmi_smt_io *io, const struct scmi_server *server)
{
	uint8_t request[SCMI_MESSAGE_MAX];
	uint8_t response[SCMI_RESPONSE_MAX];
	uint32_t flags, length, status;
	size_t response_size = 0;

	status = io->read32(io->context, CHANNEL_STATUS);
	if (status & SCMI_CHANNEL_FREE) {
		return false;
	}
	io->barrier(io->context);
	flags = io->read32(io->context, FLAGS);
	length = io->read32(io->context, LENGTH);
	if (status == 0 && !(flags & ~INTERRUPT_ENABLED) && length >= 4 &&
	    length <= sizeof(request) && !(length & 3)) {
		for (size_t offset = 0; offset < length; offset += 4) {
			scmi_put_le32(request + offset,
				      io->read32(io->context, SCMI_SMT_HEADER_OFFSET + offset));
		}
		response_size = scmi_dispatch(server, request, length, response, sizeof(response));
	}
	if (response_size) {
		for (size_t offset = 0; offset < response_size; offset += 4) {
			io->write32(io->context, SCMI_SMT_HEADER_OFFSET + offset,
				    scmi_get_le32(response + offset));
		}
		io->write32(io->context, LENGTH, (uint32_t)response_size);
	}
	io->barrier(io->context);
	io->write32(io->context, CHANNEL_STATUS,
		    SCMI_CHANNEL_FREE | (response_size ? 0 : SCMI_CHANNEL_ERROR));
	io->barrier(io->context);
	if (flags & INTERRUPT_ENABLED) {
		io->notify(io->context);
	}
	return true;
}

bool scmi_smt_free(const struct scmi_smt_io *io)
{
	return io->read32(io->context, CHANNEL_STATUS) & SCMI_CHANNEL_FREE;
}

bool scmi_smt_send(const struct scmi_smt_io *io, const uint8_t *message, size_t size)
{
	if (!scmi_smt_free(io) || size < 4 || size > SCMI_MESSAGE_MAX || (size & 3)) {
		return false;
	}
	io->barrier(io->context);
	for (size_t offset = 0; offset < size; offset += 4) {
		io->write32(io->context, SCMI_SMT_HEADER_OFFSET + offset,
			    scmi_get_le32(message + offset));
	}
	io->write32(io->context, FLAGS, 0);
	io->write32(io->context, LENGTH, (uint32_t)size);
	io->barrier(io->context);
	io->write32(io->context, CHANNEL_STATUS, 0);
	io->barrier(io->context);
	io->notify(io->context);
	return true;
}
