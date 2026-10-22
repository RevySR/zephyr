/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_RESET_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_RESET_H_

#include <zephyr/mgmt/scmi/server.h>

struct scmi_reset_domain {
	const char *name;
	void *context;
	uint32_t latency_us;
	/* Explicit signal control and an optional complete reset sequence. */
	int (*set_asserted)(void *context, bool asserted);
	int (*reset)(void *context);
};

size_t scmi_reset_dispatch(const struct scmi_server *server, const uint8_t *request,
			   size_t request_size, uint8_t *response, size_t response_capacity);

#endif
