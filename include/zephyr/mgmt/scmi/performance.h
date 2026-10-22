/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_PERFORMANCE_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_PERFORMANCE_H_

#include <zephyr/mgmt/scmi/server.h>

/* Levels and frequencies use kHz; power is an abstract relative cost. */
struct scmi_performance_level {
	uint32_t khz;
	uint32_t power;
	uint16_t latency_us;
};

struct scmi_performance_domain {
	const char *name;
	const struct scmi_performance_level *levels;
	uint16_t count;
	uint32_t sustained_khz;
	uint32_t rate_limit_us;
	int (*get_level)(void *context, uint32_t *khz);
	int (*set_level)(void *context, uint32_t khz);
	void *context;
};

size_t scmi_performance_dispatch(const struct scmi_server *server, const uint8_t *request,
				size_t request_size, uint8_t *response, size_t response_capacity);

#endif
