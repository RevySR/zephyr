/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_CLIENT_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_CLIENT_H_
#include <zephyr/mgmt/scmi/server.h>

/* AP-side polling client. The caller serializes access across CPUs/firmware.
 * IO uses the same SMT layout as the server; notify rings the AON doorbell.
 * Reinitialize a poisoned client only after a verified channel/server reset.
 */
struct scmi_client {
	struct scmi_smt_io io;
	uint32_t (*ticks)(void *context);
	uint32_t timeout_ticks, poll_limit;
	uint16_t next_token;
	bool busy, poisoned;
};

/* Returns local -errno; remote SCMI status is separate. Payload excludes header/status. */
int scmi_client_call(struct scmi_client *client, uint8_t protocol, uint8_t command,
		     const void *request, size_t request_size, void *response,
		     size_t response_capacity, size_t *response_size, int32_t *remote_status);
#endif
