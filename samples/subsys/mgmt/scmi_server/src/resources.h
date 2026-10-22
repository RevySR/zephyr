/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SCMI_SAMPLE_RESOURCES_H_
#define SCMI_SAMPLE_RESOURCES_H_

#include <zephyr/mgmt/scmi/server.h>

int scmi_resources_init(struct scmi_server *server);
/* Periodic platform work from the firmware's polling loop. */
void scmi_resources_poll(void);

#endif
