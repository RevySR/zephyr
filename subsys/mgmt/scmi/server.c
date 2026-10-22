/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/server.h>

size_t scmi_dispatch(const struct scmi_server *server, const uint8_t *request, size_t request_size,
		     uint8_t *response, size_t response_capacity)
{
	uint32_t header, protocol;
	int32_t status = SCMI_NOT_SUPPORTED;

	if (request_size < 4 || response_capacity < 8) {
		return 0;
	}
	header = scmi_get_le32(request);
	protocol = (header >> 10) & 0xffU;
	if (header & 0xf0000300U) {
		status = SCMI_PROTOCOL_ERROR;
	} else if (protocol == SCMI_BASE_PROTOCOL) {
		return scmi_base_dispatch(server, request, request_size, response,
					  response_capacity);
	}
	scmi_put_le32(response, header & 0x0ffffcffU);
	scmi_put_le32(response + 4, (uint32_t)status);
	return 8;
}
