/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_CLOCK_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_CLOCK_H_

#include <zephyr/mgmt/scmi/server.h>

/* Rates are Hz, in strictly increasing order. Callbacks return negative errno. */
struct scmi_clock_ops {
	int (*describe_rate)(void *context, unsigned int index, uint64_t *rate);
	int (*get_rate)(void *context, uint64_t *rate);
	int (*set_rate)(void *context, uint64_t rate);
	int (*get_enabled)(void *context, bool *enabled);
	int (*set_enabled)(void *context, bool enabled);
};

struct scmi_clock {
	const char *name;
	const struct scmi_clock_ops *ops;
	void *context;
	uint16_t rate_count;
	uint32_t enable_latency_us;
};

size_t scmi_clock_dispatch(const struct scmi_server *server, const uint8_t *request,
			   size_t request_size, uint8_t *response, size_t response_capacity);

#endif
