/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/reset.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include "resource.h"

size_t scmi_reset_dispatch(const struct scmi_server *server, const uint8_t *request,
			   size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t request_sizes[] = {4, 4, 8, 8, 16};
	static const uint8_t response_sizes[] = {12, 12, 12, 32, 8};
	uint8_t result[32] = {0};
	const struct scmi_reset_domain *domain;
	struct scmi_reset_domain description = {0};
	char name[16] = {0};
	uint32_t command, id, flags;
	int32_t status = SCMI_SUCCESS;
	size_t size = 8;

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
		scmi_put_le32(result + 8, 0x30000); /* Reset Domain 3.0 */
		goto done;
	}
	if (command == 1) {
		scmi_put_le32(result + 8, server->reset_count);
		goto done;
	}
	id = scmi_get_le32(request + 4);
	if (command == 2) {
		status = id < ARRAY_SIZE(request_sizes) ? SCMI_SUCCESS : SCMI_NOT_FOUND;
		goto done;
	}
	if (id >= server->reset_count) {
		status = SCMI_NOT_FOUND;
		goto done;
	}
	if (server->reset_lookup) {
		status = scmi_resource_status(
			server->reset_lookup(server->resource_context, id, &description, name));
		if (status) {
			goto done;
		}
		domain = &description;
	} else {
		domain = &server->reset_domains[id];
	}
	if (!domain->name) {
		status = SCMI_HARDWARE_ERROR;
		goto done;
	}
	if (command == 3) {
		scmi_put_le32(result + 12, domain->latency_us);
		scmi_resource_name(result + 16, domain->name);
		goto done;
	}
	flags = scmi_get_le32(request + 8);
	if ((flags & ~7U) || scmi_get_le32(request + 12) ||
	    ((flags & BIT(2)) && !(flags & BIT(0)))) {
		status = SCMI_INVALID_PARAMETERS;
	} else if (flags & BIT(2)) {
		status = SCMI_NOT_SUPPORTED;
	} else if (flags & BIT(0)) {
		status = domain->reset ? scmi_resource_status(domain->reset(domain->context))
				       : SCMI_NOT_SUPPORTED;
	} else {
		status = domain->set_asserted ? scmi_resource_status(domain->set_asserted(
							domain->context, flags & BIT(1)))
					      : SCMI_DENIED;
	}
done:
	if (status) {
		size = 8;
	}
	scmi_put_le32(result + 4, status);
	memcpy(response, result, size);
	return size;
}
