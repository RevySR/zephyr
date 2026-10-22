/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_SUBSYS_MGMT_SCMI_RESOURCE_H_
#define ZEPHYR_SUBSYS_MGMT_SCMI_RESOURCE_H_

#include <zephyr/mgmt/scmi/server.h>
#include <errno.h>

static inline int32_t scmi_resource_status(int error)
{
	switch (error) {
	case 0:
		return SCMI_SUCCESS;
	case -EINVAL:
		return SCMI_INVALID_PARAMETERS;
	case -ERANGE:
		return SCMI_OUT_OF_RANGE;
	case -EACCES:
	case -EPERM:
		return SCMI_DENIED;
	case -ENOENT:
		return SCMI_NOT_FOUND;
	case -ENOSYS:
	case -ENOTSUP:
		return SCMI_NOT_SUPPORTED;
	case -EBUSY:
		return SCMI_BUSY;
	case -ETIMEDOUT:
		return SCMI_COMMS_ERROR;
	default:
		return SCMI_HARDWARE_ERROR;
	}
}

/* The destination must be a zero-initialized 16-byte field. */
static inline void scmi_resource_name(uint8_t *out, const char *name)
{
	for (size_t i = 0; i < 15 && name[i]; i++) {
		out[i] = name[i];
	}
}

static inline void scmi_resource_rate(uint8_t *out, uint64_t rate)
{
	scmi_put_le32(out, rate);
	scmi_put_le32(out + 4, rate >> 32);
}

#endif
