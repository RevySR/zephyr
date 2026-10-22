/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_MGMT_SCMI_REGULATOR_H_
#define ZEPHYR_INCLUDE_MGMT_SCMI_REGULATOR_H_
#include <zephyr/device.h>
#include <zephyr/mgmt/scmi/voltage.h>
int scmi_regulator_domain_init(struct scmi_voltage_domain *domain, const struct device *dev,
			       const char *name, bool always_on);
#endif
