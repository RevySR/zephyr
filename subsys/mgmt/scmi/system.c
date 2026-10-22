/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/server.h>
#include <string.h>
#include "resource.h"

size_t scmi_system_dispatch(const struct scmi_server *server, const uint8_t *request,
			    size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t sizes[] = {4, 4, 8, 12};
	uint8_t result[12] = {0};
	uint32_t command, state, flags;
	int32_t status = SCMI_SUCCESS;
	size_t size = 8;

	if (request_size < 4 || response_capacity < 8) {
		return 0;
	}
	scmi_put_le32(result, scmi_get_le32(request));
	command = request[0];
	if (command > 3) {
		status = SCMI_NOT_SUPPORTED;
		goto done;
	}
	if (request_size != sizes[command]) {
		status = SCMI_INVALID_PARAMETERS;
		goto done;
	}
	size = command == 3 ? 8 : 12;
	if (response_capacity < size) {
		return 0;
	}
	switch (command) {
	case 0:
		scmi_put_le32(result + 8, 0x20000);
		break;
	case 1:
		break;
	case 2:
		state = scmi_get_le32(request + 4);
		if (state > 3) {
			status = state == 5 ? SCMI_NOT_SUPPORTED : SCMI_NOT_FOUND;
		}
		break;
	case 3:
		flags = scmi_get_le32(request + 4);
		state = scmi_get_le32(request + 8);
		if ((flags & ~1U) || state > 4) {
			status = SCMI_INVALID_PARAMETERS;
		} else if (flags || state > 1) {
			status = SCMI_NOT_SUPPORTED;
		} else {
			status = scmi_resource_status(
				server->system_request(server->system_context, state));
		}
		break;
	}
done:
	if (status) {
		size = 8;
	}
	scmi_put_le32(result + 4, status);
	memcpy(response, result, size);
	return size;
}
