/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/mgmt/scmi/server.h>
#if defined(CONFIG_SCMI_SERVER_POWER)
#include <zephyr/mgmt/scmi/power.h>
#endif
#if defined(CONFIG_SCMI_SERVER_CLOCK)
#include <zephyr/mgmt/scmi/clock.h>
#endif
#if defined(CONFIG_SCMI_SERVER_RESET)
#include <zephyr/mgmt/scmi/reset.h>
#endif
#if defined(CONFIG_SCMI_SERVER_PERFORMANCE)
#include <zephyr/mgmt/scmi/performance.h>
#endif
#if defined(CONFIG_SCMI_SERVER_VOLTAGE)
#include <zephyr/mgmt/scmi/voltage.h>
#endif

size_t scmi_dispatch(const struct scmi_server *server, const uint8_t *request, size_t request_size,
		     uint8_t *response, size_t response_capacity)
{
	uint32_t header, protocol;
	int32_t status = SCMI_NOT_SUPPORTED;

	if (request_size < 4 || response_capacity < 8) {
		return 0;
	}
	header = scmi_get_le32(request);
	protocol = (header >> 10) & 0xffU;
	if (header & 0xf0000300U) {
		status = SCMI_PROTOCOL_ERROR;
	} else if (protocol == SCMI_BASE_PROTOCOL) {
		return scmi_base_dispatch(server, request, request_size, response,
					  response_capacity);
#if defined(CONFIG_SCMI_SERVER_POWER)
	} else if (protocol == SCMI_POWER_PROTOCOL && server->power_count) {
		return scmi_power_dispatch(server, request, request_size, response,
					   response_capacity);
#endif
#if defined(CONFIG_SCMI_SERVER_CLOCK)
	} else if (protocol == SCMI_CLOCK_PROTOCOL && server->clock_count) {
		return scmi_clock_dispatch(server, request, request_size, response,
					   response_capacity);
#endif
#if defined(CONFIG_SCMI_SERVER_RESET)
	} else if (protocol == SCMI_RESET_PROTOCOL && server->reset_count) {
		return scmi_reset_dispatch(server, request, request_size, response,
					   response_capacity);
#endif
#if defined(CONFIG_SCMI_SERVER_PERFORMANCE)
	} else if (protocol == SCMI_PERFORMANCE_PROTOCOL && server->performance_count) {
		return scmi_performance_dispatch(server, request, request_size, response,
						 response_capacity);
#endif
#if defined(CONFIG_SCMI_SERVER_VOLTAGE)
	} else if (protocol == SCMI_VOLTAGE_PROTOCOL && server->voltage_count) {
		return scmi_voltage_dispatch(server, request, request_size, response,
					     response_capacity);
#endif
	}
	scmi_put_le32(response, header & 0x0ffffcffU);
	scmi_put_le32(response + 4, (uint32_t)status);
	return 8;
}
