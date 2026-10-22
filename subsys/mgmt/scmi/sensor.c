/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/sensor.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include "resource.h"

static bool supported(uint32_t command)
{
	return command <= 3 || command == 6 || command == 9 || command == 10;
}

size_t scmi_sensor_dispatch(const struct scmi_server *server, const uint8_t *request,
			    size_t request_size, uint8_t *response, size_t response_capacity)
{
	static const uint8_t sizes[] = {4, 4, 8, 8, 0, 0, 12, 0, 0, 8, 12};
	uint8_t result[SCMI_RESPONSE_MAX] = {0};
	struct scmi_sensor sensor = {0};
	char name[16] = {0};
	uint32_t command, id;
	int32_t status = SCMI_SUCCESS;
	size_t size = 8;
	int64_t value;

	if (request_size < 4 || response_capacity < 8) {
		return 0;
	}
	scmi_put_le32(result, scmi_get_le32(request));
	command = request[0];
	if (!supported(command)) {
		status = SCMI_NOT_SUPPORTED;
		goto done;
	}
	if (request_size != sizes[command]) {
		status = SCMI_INVALID_PARAMETERS;
		goto done;
	}
	size = command == 1 ? 24
			    : (command == 3 ? 40 : (command == 6 ? 16 : (command == 10 ? 8 : 12)));
	if (response_capacity < size) {
		return 0;
	}
	if (command == 0) {
		scmi_put_le32(result + 8, 0x20000);
		goto done;
	}
	if (command == 1) {
		scmi_put_le32(result + 8, server->sensor_count);
		goto done;
	}
	id = scmi_get_le32(request + 4);
	if (command == 2) {
		status = supported(id) ? SCMI_SUCCESS : SCMI_NOT_FOUND;
		goto done;
	}
	if (id >= server->sensor_count) {
		status = command == 3 ? SCMI_OUT_OF_RANGE : SCMI_NOT_FOUND;
		goto done;
	}
	if (!server->sensor_lookup) {
		status = SCMI_HARDWARE_ERROR;
		goto done;
	}
	if (command == 9 || command == 10) {
		uint32_t config = 0;

		status = scmi_resource_status(
			server->sensor_lookup(server->resource_context, id, &sensor, name));
		if (status) {
			goto done;
		}
		if (command == 9) {
			status = sensor.get_config ? scmi_resource_status(sensor.get_config(
							     sensor.context, &config))
						   : SCMI_NOT_SUPPORTED;
			scmi_put_le32(result + 8, config);
		} else {
			config = scmi_get_le32(request + 8);
			status = sensor.set_config ? scmi_resource_status(sensor.set_config(
							     sensor.context, config))
						   : SCMI_NOT_SUPPORTED;
		}
		goto done;
	}
	if (command == 3) {
		unsigned int count = MIN(server->sensor_count - id,
					 (MIN(response_capacity, sizeof(result)) - 12) / 28);

		for (unsigned int i = 0; i < count; i++) {
			uint8_t *desc = result + 12 + i * 28;

			status = scmi_resource_status(server->sensor_lookup(
				server->resource_context, id + i, &sensor, name));
			if (status) {
				goto done;
			}
			if (!sensor.name || sensor.scale < -16 || sensor.scale > 15) {
				status = SCMI_HARDWARE_ERROR;
				goto done;
			}
			scmi_put_le32(desc, id + i);
			scmi_put_le32(desc + 8,
				      (((uint32_t)sensor.scale & 0x1f) << 11) | sensor.type);
			scmi_resource_name(desc + 12, sensor.name);
		}
		scmi_put_le32(result + 8, ((server->sensor_count - id - count) << 16) | count);
		size = 12 + count * 28;
	} else {
		uint32_t flags = scmi_get_le32(request + 8);

		if (flags) {
			status = flags == 1 ? SCMI_NOT_SUPPORTED : SCMI_INVALID_PARAMETERS;
			goto done;
		}
		status = scmi_resource_status(
			server->sensor_lookup(server->resource_context, id, &sensor, name));
		if (!status) {
			status = sensor.read
					 ? scmi_resource_status(sensor.read(sensor.context, &value))
					 : SCMI_HARDWARE_ERROR;
		}
		if (!status) {
			scmi_resource_rate(result + 8, (uint64_t)value);
		}
	}
done:
	if (status) {
		size = 8;
	}
	scmi_put_le32(result + 4, status);
	memcpy(response, result, size);
	return size;
}
