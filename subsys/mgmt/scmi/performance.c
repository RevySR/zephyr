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

/* Base DISCOVER_AGENT reports the AP as agent 1; 0 is the platform. */
#define AP_AGENT 1U

static bool supported(const struct scmi_server *server, uint32_t command)
{
	/* No level notifications or fast channels. */
	return command <= 8 || (command == 9 && server->notifications);
}

/* Levels are sorted by ascending frequency. */
static void get_limits(const struct scmi_performance_domain *domain, uint32_t *min, uint32_t *max)
{
	const struct scmi_performance_state *state = domain->state;

	*min = state && state->max_khz ? state->min_khz : domain->levels[0].khz;
	*max = state && state->max_khz ? state->max_khz : domain->levels[domain->count - 1].khz;
}

static bool limited(const struct scmi_performance_domain *domain)
{
	uint32_t min, max;

	get_limits(domain, &min, &max);
	return min != domain->levels[0].khz || max != domain->levels[domain->count - 1].khz;
}

/* Limits are narrowed to the supported levels they enclose. */
static int32_t set_limits(const struct scmi_performance_domain *domain, uint32_t min, uint32_t max)
{
	struct scmi_performance_state *state = domain->state;
	uint32_t old_min, old_max, level, low = 0, high = 0;
	int ret;

	if (!state) {
		return SCMI_NOT_SUPPORTED;
	}
	if (min > max) {
		return SCMI_INVALID_PARAMETERS;
	}
	for (size_t i = 0; i < domain->count; i++) {
		uint32_t khz = domain->levels[i].khz;

		if (khz >= min && khz <= max) {
			low = low ? low : khz;
			high = khz;
		}
	}
	if (!low || min < domain->levels[0].khz ||
	    max > domain->levels[domain->count - 1].khz) {
		return SCMI_OUT_OF_RANGE;
	}
	ret = domain->get_level(domain->context, &level);
	if (!ret && (level < low || level > high)) {
		ret = domain->set_level(domain->context, level > high ? high : low);
	}
	if (ret) {
		return status_from_errno(ret);
	}
	get_limits(domain, &old_min, &old_max);
	state->min_khz = low;
	state->max_khz = high;
	if (low != old_min || high != old_max) {
		state->agent = AP_AGENT;
		state->pending = state->notify;
	}
	return SCMI_SUCCESS;
}

size_t scmi_performance_dispatch(const struct scmi_server *server, const uint8_t *request,
				size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t requests[] = {4, 4, 8, 8, 12, 16, 8, 12, 8, 12};
	static const uint8_t responses[] = {12, 24, 12, 40, 12, 8, 16, 8, 12, 8};
	uint8_t result[SCMI_MESSAGE_MAX] = {0};
	const struct scmi_performance_domain *domain;
	size_t size = 8, capacity = MIN(response_capacity, sizeof(result));
	uint32_t command = request_size ? request[0] : UINT32_MAX;
	uint32_t id, level = 0, flags, min, max;
	int32_t status = SCMI_SUCCESS;

	if (request_size < 4 || capacity < 8) {
		return 0;
	}
	scmi_put_le32(result, scmi_get_le32(request));
	if (!supported(server, command)) {
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
		status = supported(server, id) ? SCMI_SUCCESS : SCMI_NOT_FOUND;
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
		scmi_put_le32(result + 8, BIT(30) | (domain->state ? BIT(31) : 0) |
					      (domain->state && server->notifications ? BIT(29) : 0));
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
	case 5: /* LIMITS_SET */
		status = set_limits(domain, scmi_get_le32(request + 12), scmi_get_le32(request + 8));
		break;
	case 6: /* LIMITS_GET */
		get_limits(domain, &min, &max);
		scmi_put_le32(result + 8, max);
		scmi_put_le32(result + 12, min);
		break;
	case 7: /* LEVEL_SET */
		level = scmi_get_le32(request + 8);
		get_limits(domain, &min, &max);
		status = SCMI_OUT_OF_RANGE;
		if (level < min || level > max) {
			break;
		}
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
	case 9: /* NOTIFY_LIMITS */
		flags = scmi_get_le32(request + 8);
		if (flags & ~1U) {
			status = SCMI_INVALID_PARAMETERS;
		} else if (!domain->state) {
			status = SCMI_NOT_SUPPORTED;
		} else {
			/* A (re)registering agent starts without the current limits. */
			domain->state->notify = flags;
			domain->state->pending = flags && limited(domain);
			domain->state->resend |= flags;
		}
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

size_t scmi_performance_notification(const struct scmi_server *server, uint8_t *message,
				     size_t capacity)
{
	for (uint32_t id = 0; id < server->performance_count && capacity >= 20; id++) {
		const struct scmi_performance_domain *domain = &server->performance_domains[id];
		struct scmi_performance_state *state = domain->state;
		uint32_t min, max;

		if (!state || !state->pending) {
			continue;
		}
		state->pending = false;
		get_limits(domain, &min, &max);
		/* PERFORMANCE_LIMITS_CHANGED: message 0, notification type 3. */
		scmi_put_le32(message, 3U << 8 | SCMI_PERFORMANCE_PROTOCOL << 10);
		scmi_put_le32(message + 4, state->agent);
		scmi_put_le32(message + 8, id);
		scmi_put_le32(message + 12, max);
		scmi_put_le32(message + 16, min);
		return 20;
	}
	return 0;
}

bool scmi_performance_take_resend(const struct scmi_server *server)
{
	bool resend = false;

	for (uint32_t id = 0; id < server->performance_count; id++) {
		struct scmi_performance_state *state = server->performance_domains[id].state;

		if (state) {
			resend |= state->resend;
			state->resend = false;
		}
	}
	return resend;
}
