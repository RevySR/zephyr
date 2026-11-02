/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_SENSOR_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_SENSOR_H_

#include <zephyr/mgmt/scmi/server.h>
#include <zephyr/sys/util.h>

/* Sensor types, the unit of a reported value. */
#define SCMI_SENSOR_TYPE_DEGREES_C 2U
#define SCMI_SENSOR_TYPE_VOLTS     5U

/* SENSOR_CONFIG_GET/SET fields. */
#define SCMI_SENSOR_CONFIG_ENABLED  BIT(0)
#define SCMI_SENSOR_CONFIG_RESERVED GENMASK(8, 2)

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
