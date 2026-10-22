/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_VOLTAGE_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_VOLTAGE_H_
#include <zephyr/mgmt/scmi/server.h>

/* Backends return zero or a negative errno; values are microvolts. */
struct scmi_voltage_ops {
	int (*get_level)(void *context, int32_t *uv);
	int (*set_level)(void *context, int32_t uv);
	int (*get_enabled)(void *context, bool *enabled);
	int (*set_enabled)(void *context, bool enabled);
};

struct scmi_voltage_domain {
	const char *name;
	int32_t min_uv;
	int32_t max_uv;
	int32_t step_uv;
	bool always_on;
	const struct scmi_voltage_ops *ops;
	void *context;
	bool read_only;
};

size_t scmi_voltage_dispatch(const struct scmi_server *server, const uint8_t *request,
			     size_t request_size, uint8_t *response, size_t response_capacity);
#endif
