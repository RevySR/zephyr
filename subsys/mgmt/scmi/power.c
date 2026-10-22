/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/power.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include "resource.h"

#define POWER_OFF BIT(30)

size_t scmi_power_dispatch(const struct scmi_server *server, const uint8_t *request,
			   size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t request_sizes[] = {4, 4, 8, 8, 16, 8};
	static const uint8_t response_sizes[] = {12, 24, 12, 28, 8, 12};
	uint8_t result[28] = {0};
	const struct scmi_power_domain *domain;
	uint32_t command, id, flags, state;
	int32_t status = SCMI_SUCCESS;
	size_t size = 8;
	bool on;

	if (request_size < 4 || response_capacity < 8) {
		return 0;
	}
	scmi_put_le32(result, scmi_get_le32(request));
	command = request[0];
	if (command >= ARRAY_SIZE(request_sizes)) {
		status = SCMI_NOT_SUPPORTED;
		goto done;
	}
	if (request_size != request_sizes[command]) {
		status = SCMI_INVALID_PARAMETERS;
		goto done;
	}
	if (response_capacity < response_sizes[command]) {
		return 0;
	}
	size = response_sizes[command];
	if (command == 0) {
		scmi_put_le32(result + 8, 0x30000);
		goto done;
	}
	if (command == 1) {
		scmi_put_le32(result + 8, server->power_count);
		goto done;
	}
	id = scmi_get_le32(request + (command == 4 ? 8 : 4));
	if (command == 2) {
		status = id < ARRAY_SIZE(request_sizes) ? SCMI_SUCCESS : SCMI_NOT_FOUND;
		goto done;
	}
	id &= 0xffff;
	if (id >= server->power_count) {
		status = SCMI_NOT_FOUND;
		goto done;
	}
	domain = &server->power_domains[id];
	if (!domain->name || !domain->get_on) {
		status = SCMI_HARDWARE_ERROR;
		goto done;
	}
	switch (command) {
	case 3:
		scmi_put_le32(result + 8, domain->set_on ? BIT(29) : 0);
		scmi_resource_name(result + 12, domain->name);
		break;
	case 4:
		flags = scmi_get_le32(request + 4);
		state = scmi_get_le32(request + 12);
		if ((flags & ~1U) || (state != 0 && state != POWER_OFF)) {
			status = SCMI_INVALID_PARAMETERS;
		} else if (flags) {
			status = SCMI_NOT_SUPPORTED;
		} else if (!domain->set_on) {
			status = SCMI_DENIED;
		} else {
			status = scmi_resource_status(domain->set_on(domain->context, state == 0));
		}
		break;
	case 5:
		status = scmi_resource_status(domain->get_on(domain->context, &on));
		if (!status) {
			scmi_put_le32(result + 8, on ? 0 : POWER_OFF);
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
