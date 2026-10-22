/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/clock.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include "resource.h"

static int32_t set_rate(const struct scmi_clock *clock, uint32_t flags, uint64_t requested)
{
	uint64_t selected = 0, distance = UINT64_MAX, previous = 0;
	bool found = false;

	if (flags & ~0xfU) {
		return SCMI_INVALID_PARAMETERS;
	}
	if (flags & BIT(0)) {
		return SCMI_NOT_SUPPORTED;
	}
	if (!clock->ops->set_rate) {
		return SCMI_DENIED;
	}
	for (unsigned int i = 0; i < clock->rate_count; i++) {
		uint64_t rate, delta;
		int ret = clock->ops->describe_rate(clock->context, i, &rate);

		if (ret) {
			return scmi_resource_status(ret);
		}
		if (!rate || (i && rate <= previous)) {
			return SCMI_HARDWARE_ERROR;
		}
		previous = rate;
		if (!(flags & BIT(3)) && ((flags & BIT(2)) ? rate < requested : rate > requested)) {
			continue;
		}
		delta = rate > requested ? rate - requested : requested - rate;
		if (!found || delta < distance) {
			selected = rate;
			distance = delta;
			found = true;
		}
	}
	return found ? scmi_resource_status(clock->ops->set_rate(clock->context, selected))
		     : SCMI_INVALID_PARAMETERS;
}

size_t scmi_clock_dispatch(const struct scmi_server *server, const uint8_t *request,
			   size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t request_sizes[] = {4, 4, 8, 8, 12, 20, 8, 12};
	static const uint8_t response_sizes[] = {12, 12, 12, 32, 20, 8, 16, 8};
	uint8_t result[SCMI_RESPONSE_MAX] = {0};
	const struct scmi_clock *clock;
	struct scmi_clock description = {0};
	char name[16] = {0};
	uint32_t command, id;
	int32_t status = SCMI_SUCCESS;
	size_t size = 8;
	bool enabled;
	uint64_t rate;

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
		scmi_put_le32(result + 8, 0x20000); /* Clock 2.0 */
		goto done;
	}
	if (command == 1) {
		scmi_put_le32(result + 8, server->clock_count);
		goto done;
	}
	id = scmi_get_le32(request + (command == 5 ? 8 : 4));
	if (command == 2) {
		status = id < ARRAY_SIZE(request_sizes) ? SCMI_SUCCESS : SCMI_NOT_FOUND;
		goto done;
	}
	if (id >= server->clock_count) {
		status = SCMI_NOT_FOUND;
		goto done;
	}
	if (server->clock_lookup) {
		status = scmi_resource_status(
			server->clock_lookup(server->resource_context, id, &description, name));
		if (status) {
			goto done;
		}
		clock = &description;
	} else {
		clock = &server->clocks[id];
	}
	if (!clock->name || !clock->ops || !clock->ops->get_rate || !clock->ops->describe_rate ||
	    !clock->ops->get_enabled || !clock->rate_count) {
		status = SCMI_HARDWARE_ERROR;
		goto done;
	}
	switch (command) {
	case 3:
		status = scmi_resource_status(clock->ops->get_enabled(clock->context, &enabled));
		if (!status) {
			scmi_put_le32(result + 8, enabled);
			scmi_resource_name(result + 12, clock->name);
			scmi_put_le32(result + 28, clock->enable_latency_us);
		}
		break;
	case 4: {
		uint32_t index = scmi_get_le32(request + 8);
		uint32_t count;

		if (index >= clock->rate_count) {
			status = SCMI_OUT_OF_RANGE;
			break;
		}
		count = MIN((MIN(response_capacity, sizeof(result)) - 12) / 8,
			    clock->rate_count - index);
		for (unsigned int i = 0; i < count; i++) {
			status = scmi_resource_status(
				clock->ops->describe_rate(clock->context, index + i, &rate));
			if (status) {
				break;
			}
			scmi_resource_rate(result + 12 + i * 8, rate);
		}
		scmi_put_le32(result + 8, ((clock->rate_count - index - count) << 16) | count);
		size = 12 + count * 8;
		break;
	}
	case 5:
		rate = (uint64_t)scmi_get_le32(request + 16) << 32 | scmi_get_le32(request + 12);
		status = set_rate(clock, scmi_get_le32(request + 4), rate);
		break;
	case 6:
		status = scmi_resource_status(clock->ops->get_rate(clock->context, &rate));
		if (!status) {
			scmi_resource_rate(result + 8, rate);
		}
		break;
	case 7:
		if (scmi_get_le32(request + 8) > 1) {
			status = SCMI_INVALID_PARAMETERS;
		} else if (!clock->ops->set_enabled) {
			status = SCMI_DENIED;
		} else {
			status = scmi_resource_status(clock->ops->set_enabled(
				clock->context, scmi_get_le32(request + 8)));
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
