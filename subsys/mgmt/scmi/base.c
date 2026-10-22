/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/server.h>
#include <string.h>

#if defined(CONFIG_SCMI_SERVER_VOLTAGE)
#define VOLTAGE_AVAILABLE(server) ((server)->voltage_count != 0)
#else
#define VOLTAGE_AVAILABLE(server) false
#endif

#if defined(CONFIG_SCMI_SERVER_PERFORMANCE)
#define PERFORMANCE_AVAILABLE(server) ((server)->performance_count != 0)
#else
#define PERFORMANCE_AVAILABLE(server) false
#endif

#define SCMI_BASE_VERSION 0x00020000U

static void put_name(uint8_t *out, const char *name)
{
	size_t i = 0;

	memset(out, 0, 16);
	while (i < 15 && name[i]) {
		out[i] = (uint8_t)name[i];
		i++;
	}
}

size_t scmi_base_dispatch(const struct scmi_server *server, const uint8_t *request,
			  size_t request_size, uint8_t *response, size_t response_capacity)
{
	uint8_t result[28] = {0};
	uint8_t protocols[7];
	uint32_t protocol_count = 0;

	uint32_t header, command, argument = 0;
	int32_t status = 0;
	size_t size = 8;
	size_t expected;

	if (PERFORMANCE_AVAILABLE(server)) {
		protocols[protocol_count++] = SCMI_PERFORMANCE_PROTOCOL;
	}
#if defined(CONFIG_SCMI_SERVER_CLOCK)
	if (server->clock_count) {
		protocols[protocol_count++] = SCMI_CLOCK_PROTOCOL;
	}
#endif
#if defined(CONFIG_SCMI_SERVER_RESET)
	if (server->reset_count) {
		protocols[protocol_count++] = SCMI_RESET_PROTOCOL;
	}
#endif
	if (VOLTAGE_AVAILABLE(server)) {
		protocols[protocol_count++] = SCMI_VOLTAGE_PROTOCOL;
	}

	if (request_size < 4 || response_capacity < 8) {
		return 0;
	}
	header = scmi_get_le32(request);
	command = header & 0xffU;
	/* Preserve token, protocol and command; responses have message type 0. */
	scmi_put_le32(result, header & 0x0ffffcffU);
	if ((header & 0xf0000300U) != 0) {
		status = SCMI_PROTOCOL_ERROR;
	} else if (((header >> 10) & 0xffU) != SCMI_BASE_PROTOCOL || command > 7) {
		status = SCMI_NOT_SUPPORTED;
	} else {
		expected = (command == 2 || command == 6 || command == 7) ? 8 : 4;
		if (request_size != expected) {
			status = SCMI_INVALID_PARAMETERS;
		} else {
			if (expected == 8) {
				argument = scmi_get_le32(request + 4);
			}
			switch (command) {
			case 0: /* PROTOCOL_VERSION */
				scmi_put_le32(result + 8, SCMI_BASE_VERSION);
				size = 12;
				break;
			case 1: /* PROTOCOL_ATTRIBUTES: one AP agent */
				scmi_put_le32(result + 8, (1U << 8) | protocol_count);
				size = 12;
				break;
			case 2: /* PROTOCOL_MESSAGE_ATTRIBUTES */
				if (argument > 7) {
					status = SCMI_NOT_FOUND;
				} else {
					size = 12;
				}
				break;
			case 3: /* BASE_DISCOVER_VENDOR */
				put_name(result + 8, server->identity.vendor);
				size = 24;
				break;
			case 4: /* BASE_DISCOVER_SUB_VENDOR */
				put_name(result + 8, server->identity.sub_vendor);
				size = 24;
				break;
			case 5: /* BASE_DISCOVER_IMPLEMENTATION_VERSION */
				scmi_put_le32(result + 8, server->identity.implementation_version);
				size = 12;
				break;
			case 6: /* BASE_DISCOVER_LIST_PROTOCOLS: Base itself is excluded */
				if (argument > protocol_count) {
					status = SCMI_INVALID_PARAMETERS;
				} else {
					uint32_t count = protocol_count - argument;

					scmi_put_le32(result + 8, count);
					memcpy(result + 12, protocols + argument, count);
					size = 12 + ((count + 3U) & ~3U);
				}
				break;
			case 7: /* BASE_DISCOVER_AGENT: 0 is platform, 1 is AP */
				if (argument == UINT32_MAX) {
					argument = 1;
				}
				if (argument > 1) {
					status = SCMI_NOT_FOUND;
				} else {
					scmi_put_le32(result + 8, argument);
					put_name(result + 12, argument ? "ap" : "aon");
					size = 28;
				}
				break;
			}
		}
	}
	if (size > response_capacity) {
		return 0;
	}
	scmi_put_le32(result + 4, (uint32_t)status);
	memcpy(response, result, size);
	return size;
}
