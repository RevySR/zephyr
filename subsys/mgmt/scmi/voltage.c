/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/voltage.h>
#include <errno.h>
#include <string.h>

static int32_t protocol_status(int error)
{
	switch (error) {
	case 0:
		return SCMI_SUCCESS;
	case -EINVAL:
		return SCMI_INVALID_PARAMETERS;
	case -ERANGE:
		return SCMI_OUT_OF_RANGE;
	case -EACCES:
		return SCMI_DENIED;
	case -ENOSYS:
	case -ENOTSUP:
		return SCMI_NOT_SUPPORTED;
	case -EBUSY:
		return SCMI_BUSY;
	case -ETIMEDOUT:
	case -ENXIO:
		return SCMI_COMMS_ERROR;
	default:
		return SCMI_HARDWARE_ERROR;
	}
}

size_t scmi_voltage_dispatch(const struct scmi_server *server, const uint8_t *request,
			     size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t request_sizes[] = {4, 4, 8, 8, 12, 12, 8, 16, 8};
	static const uint8_t response_sizes[] = {12, 12, 12, 28, 24, 8, 12, 8, 12};
	const struct scmi_voltage_domain *domain;
	uint8_t result[28] = {0};
	uint32_t command, id, value;
	int32_t status = SCMI_SUCCESS, uv = 0;
	bool enabled = false;
	size_t size = 8;

	if (request_size < 4 || response_capacity < 8) {
		return 0;
	}
	command = request[0];
	scmi_put_le32(result, scmi_get_le32(request));
	if (command >= sizeof(request_sizes)) {
		status = SCMI_NOT_SUPPORTED;
		goto done;
	}
	if (request_size != request_sizes[command]) {
		status = SCMI_INVALID_PARAMETERS;
		goto done;
	}
	/* Never perform a mutation when the response cannot be represented. */
	if (response_capacity < response_sizes[command]) {
		return 0;
	}
	size = response_sizes[command];
	if (command == 0) {
		scmi_put_le32(result + 8, 0x10000); /* Voltage Domain 1.0 */
		goto done;
	}
	if (command == 1) {
		scmi_put_le32(result + 8, server->voltage_count);
		goto done;
	}
	id = scmi_get_le32(request + 4);
	if (command == 2) {
		status = id < sizeof(request_sizes) ? SCMI_SUCCESS : SCMI_NOT_FOUND;
		goto done;
	}
	if (id >= server->voltage_count) {
		status = SCMI_NOT_FOUND;
		goto done;
	}
	domain = &server->voltage_domains[id];
	if (domain->step_uv <= 0 || domain->min_uv > domain->max_uv || !domain->ops ||
	    ((int64_t)domain->max_uv - domain->min_uv) % domain->step_uv) {
		status = SCMI_HARDWARE_ERROR;
		goto done;
	}
	switch (command) {
	case 3: /* DOMAIN_ATTRIBUTES: synchronous, short names only. */
		for (size_t i = 0; i < 15 && domain->name[i]; i++) {
			result[12 + i] = (uint8_t)domain->name[i];
		}
		break;
	case 4: /* DESCRIBE_LEVELS: one complete linear segment. */
		if (scmi_get_le32(request + 8) != 0) {
			status = SCMI_OUT_OF_RANGE;
			break;
		}
		scmi_put_le32(result + 8, (1U << 12) | 3U);
		scmi_put_le32(result + 12, (uint32_t)domain->min_uv);
		scmi_put_le32(result + 16, (uint32_t)domain->max_uv);
		scmi_put_le32(result + 20, (uint32_t)domain->step_uv);
		break;
	case 5: /* CONFIG_SET */
		value = scmi_get_le32(request + 8);
		if (value != 0 && value != 7) {
			status = SCMI_INVALID_PARAMETERS;
		} else if (domain->read_only) {
			/* A client may confirm the inherited state without changing it. */
			if (!domain->ops->get_enabled) {
				status = SCMI_DENIED;
				break;
			}
			status = protocol_status(
				domain->ops->get_enabled(domain->context, &enabled));
			if (!status && enabled != (value == 7)) {
				status = SCMI_DENIED;
			}
		} else if (!value && domain->always_on) {
			status = SCMI_DENIED;
		} else if (!domain->ops->set_enabled) {
			status = SCMI_NOT_SUPPORTED;
		} else {
			status = protocol_status(
				domain->ops->set_enabled(domain->context, value == 7));
		}
		break;
	case 6: /* CONFIG_GET */
		status = domain->ops->get_enabled ? protocol_status(domain->ops->get_enabled(
							    domain->context, &enabled))
						  : SCMI_NOT_SUPPORTED;
		scmi_put_le32(result + 8, enabled ? 7U : 0U);
		break;
	case 7: /* LEVEL_SET: no asynchronous requests or reserved flags. */
		if (domain->read_only) {
			status = SCMI_DENIED;
			break;
		}
		value = scmi_get_le32(request + 8);
		uv = (int32_t)scmi_get_le32(request + 12);
		if (value) {
			status = value == 1 ? SCMI_NOT_SUPPORTED : SCMI_INVALID_PARAMETERS;
		} else if (uv < domain->min_uv || uv > domain->max_uv ||
			   ((int64_t)uv - domain->min_uv) % domain->step_uv) {
			status = SCMI_OUT_OF_RANGE;
		} else if (!domain->ops->set_level) {
			status = SCMI_NOT_SUPPORTED;
		} else {
			status = protocol_status(domain->ops->set_level(domain->context, uv));
		}
		break;
	case 8: /* LEVEL_GET reports the hardware, not the last requested value. */
		status = domain->ops->get_level
				 ? protocol_status(domain->ops->get_level(domain->context, &uv))
				 : SCMI_NOT_SUPPORTED;
		scmi_put_le32(result + 8, (uint32_t)uv);
		break;
	}
done:
	if (status) {
		size = 8;
	}
	if (response_capacity < size) {
		return 0;
	}
	scmi_put_le32(result + 4, (uint32_t)status);
	memcpy(response, result, size);
	return size;
}
