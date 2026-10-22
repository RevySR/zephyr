/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/performance.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

static int32_t status_from_errno(int error)
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
	case -EBUSY:
		return SCMI_BUSY;
	case -ENOTSUP:
		return SCMI_NOT_SUPPORTED;
	case -ETIMEDOUT:
	case -ENXIO:
		return SCMI_COMMS_ERROR;
	default:
		return SCMI_HARDWARE_ERROR;
	}
}

static bool supported(uint32_t command)
{
	/* Fixed platform limits; no notifications or fast channels. */
	return command <= 8 && command != 5;
}

size_t scmi_performance_dispatch(const struct scmi_server *server, const uint8_t *request,
				size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t requests[] = {4, 4, 8, 8, 12, 16, 8, 12, 8};
	static const uint8_t responses[] = {12, 24, 12, 40, 12, 8, 16, 8, 12};
	uint8_t result[SCMI_MESSAGE_MAX] = {0};
	const struct scmi_performance_domain *domain;
	size_t size = 8, capacity = MIN(response_capacity, sizeof(result));
	uint32_t command = request_size ? request[0] : UINT32_MAX;
	uint32_t id, level = 0;
	int32_t status = SCMI_SUCCESS;

	if (request_size < 4 || capacity < 8) {
		return 0;
	}
	scmi_put_le32(result, scmi_get_le32(request));
	if (!supported(command)) {
		status = SCMI_NOT_SUPPORTED;
		goto done;
	}
	if (request_size != requests[command]) {
		status = SCMI_INVALID_PARAMETERS;
		goto done;
	}
	if (capacity < responses[command]) {
		return 0;
	}
	size = responses[command];
	if (command == 0) {
		scmi_put_le32(result + 8, 0x10000); /* Performance 1.0 */
		goto done;
	}
	if (command == 1) {
		scmi_put_le32(result + 8, server->performance_count);
		goto done;
	}
	id = scmi_get_le32(request + 4);
	if (command == 2) {
		status = supported(id) ? SCMI_SUCCESS : SCMI_NOT_FOUND;
		goto done;
	}
	if (id >= server->performance_count) {
		status = SCMI_NOT_FOUND;
		goto done;
	}
	domain = &server->performance_domains[id];
	if (!domain->count || !domain->levels || !domain->get_level || !domain->set_level) {
		status = SCMI_HARDWARE_ERROR;
		goto done;
	}
	switch (command) {
	case 3: /* DOMAIN_ATTRIBUTES */
		scmi_put_le32(result + 8, 1U << 30);
		scmi_put_le32(result + 12, domain->rate_limit_us);
		scmi_put_le32(result + 16, domain->sustained_khz);
		scmi_put_le32(result + 20, domain->sustained_khz);
		for (size_t i = 0; i < 15 && domain->name[i]; i++) {
			result[24 + i] = domain->name[i];
		}
		break;
	case 4: { /* DESCRIBE_LEVELS; paginate to the caller's actual buffer. */
		uint32_t first = scmi_get_le32(request + 8);
		uint32_t count;

		if (first >= domain->count) {
			status = SCMI_OUT_OF_RANGE;
			break;
		}
		count = MIN((capacity - 12) / 12, domain->count - first);
		if (!count) {
			return 0;
		}
		scmi_put_le32(result + 8, count | ((domain->count - first - count) << 16));
		for (uint32_t i = 0; i < count; i++) {
			const struct scmi_performance_level *opp = &domain->levels[first + i];

			scmi_put_le32(result + 12 + i * 12, opp->khz);
			scmi_put_le32(result + 16 + i * 12, opp->power);
			scmi_put_le32(result + 20 + i * 12, opp->latency_us);
		}
		size = 12 + count * 12;
		break;
	}
	case 6: /* LIMITS_GET */
		scmi_put_le32(result + 8, domain->levels[domain->count - 1].khz);
		scmi_put_le32(result + 12, domain->levels[0].khz);
		break;
	case 7: /* LEVEL_SET */
		level = scmi_get_le32(request + 8);
		status = SCMI_OUT_OF_RANGE;
		for (size_t i = 0; i < domain->count; i++) {
			if (level == domain->levels[i].khz) {
				status = status_from_errno(
					domain->set_level(domain->context, level));
				break;
			}
		}
		break;
	case 8: /* LEVEL_GET must report hardware readback. */
		status = status_from_errno(domain->get_level(domain->context, &level));
		scmi_put_le32(result + 8, level);
		break;
	}
done:
	if (status) {
		size = 8;
	}
	scmi_put_le32(result + 4, (uint32_t)status);
	memcpy(response, result, size);
	return size;
}
