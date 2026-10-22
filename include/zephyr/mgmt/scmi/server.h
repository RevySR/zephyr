/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_SERVER_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_SERVER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCMI_SMT_SIZE              256U
#define SCMI_SMT_HEADER_OFFSET     24U
#define SCMI_MESSAGE_MAX           (SCMI_SMT_SIZE - SCMI_SMT_HEADER_OFFSET)
/* Responses fit the minimum 128-byte SMT layout used by AP transports. */
#define SCMI_RESPONSE_MAX          104U
#define SCMI_CHANNEL_FREE          1U
#define SCMI_CHANNEL_ERROR         2U
#define SCMI_BASE_PROTOCOL         0x10U
#define SCMI_POWER_PROTOCOL        0x11U
#define SCMI_SYSTEM_POWER_PROTOCOL 0x12U
#define SCMI_PERFORMANCE_PROTOCOL  0x13U
#define SCMI_CLOCK_PROTOCOL        0x14U
#define SCMI_SENSOR_PROTOCOL       0x15U
#define SCMI_RESET_PROTOCOL        0x16U
#define SCMI_VOLTAGE_PROTOCOL      0x17U

enum scmi_status {
	SCMI_SUCCESS = 0,
	SCMI_NOT_SUPPORTED = -1,
	SCMI_INVALID_PARAMETERS = -2,
	SCMI_DENIED = -3,
	SCMI_NOT_FOUND = -4,
	SCMI_OUT_OF_RANGE = -5,
	SCMI_BUSY = -6,
	SCMI_COMMS_ERROR = -7,
	SCMI_GENERIC_ERROR = -8,
	SCMI_HARDWARE_ERROR = -9,
	SCMI_PROTOCOL_ERROR = -10,
};

struct scmi_identity {
	const char *vendor;
	const char *sub_vendor;
	uint32_t implementation_version;
};

struct scmi_voltage_domain;
struct scmi_performance_domain;
struct scmi_clock;
struct scmi_reset_domain;
struct scmi_power_domain;
struct scmi_sensor;
struct scmi_server {
	struct scmi_identity identity;
	const struct scmi_voltage_domain *voltage_domains;
	uint16_t voltage_count;
	const struct scmi_performance_domain *performance_domains;
	uint16_t performance_count;
	const struct scmi_clock *clocks;
	uint16_t clock_count;
	const struct scmi_reset_domain *reset_domains;
	uint16_t reset_count;
	const struct scmi_power_domain *power_domains;
	uint16_t power_count;
	/* Optional on-demand descriptions for platforms with many resources. */
	void *resource_context;
	int (*clock_lookup)(void *context, uint32_t id, struct scmi_clock *clock, char name[16]);
	int (*reset_lookup)(void *context, uint32_t id, struct scmi_reset_domain *reset,
			    char name[16]);
	uint16_t sensor_count;
	int (*sensor_lookup)(void *context, uint32_t id, struct scmi_sensor *sensor, char name[16]);
};

/* Input and output are distinct byte buffers, including the SCMI header. */
size_t scmi_dispatch(const struct scmi_server *server, const uint8_t *request, size_t request_size,
		     uint8_t *response, size_t response_capacity);
size_t scmi_base_dispatch(const struct scmi_server *server, const uint8_t *request,
			  size_t request_size, uint8_t *response, size_t response_capacity);

/* IO callbacks transfer little-endian words, as host integer values. */
struct scmi_smt_io {
	void *context;
	uint32_t (*read32)(void *context, size_t offset);
	void (*write32)(void *context, size_t offset, uint32_t value);
	void (*barrier)(void *context);
	void (*notify)(void *context);
};

/* Call only at controlled cold startup, with the agent quiescent. */
void scmi_smt_init(const struct scmi_smt_io *io);
/* Call once per received doorbell, never merely upon observing BUSY. */
bool scmi_smt_respond(const struct scmi_smt_io *io, const struct scmi_server *server);

static inline uint32_t scmi_get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void scmi_put_le32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}
#endif
