/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_SENSOR_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_SENSOR_H_

#include <zephyr/mgmt/scmi/server.h>

struct scmi_sensor {
	const char *name;
	void *context;
	uint8_t type;
	int8_t scale;
	int (*read)(void *context, int64_t *value);
	int (*get_config)(void *context, uint32_t *config);
	int (*set_config)(void *context, uint32_t config);
};

size_t scmi_sensor_dispatch(const struct scmi_server *server, const uint8_t *request,
			    size_t request_size, uint8_t *response, size_t response_capacity);

#endif
