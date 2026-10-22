/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_POWER_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_POWER_H_

#include <zephyr/mgmt/scmi/server.h>

struct scmi_power_domain {
	const char *name;
	void *context;
	int (*set_on)(void *context, bool on);
	int (*get_on)(void *context, bool *on);
};

size_t scmi_power_dispatch(const struct scmi_server *server, const uint8_t *request,
			   size_t request_size, uint8_t *response, size_t response_capacity);

#endif
