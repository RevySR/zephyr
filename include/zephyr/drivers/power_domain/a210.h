/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_POWER_DOMAIN_A210_H_
#define ZEPHYR_INCLUDE_DRIVERS_POWER_DOMAIN_A210_H_
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

/* PCU register holding the current power state of a domain. */
#define A210_PCU_CURRENT_STATE 0x48
#define A210_PCU_STATE_MASK    GENMASK(4, 0)
#define A210_PCU_STATE_ON      0x1f

/* a210_system_request() states, numbered as SCMI system power states. */
#define A210_SYSTEM_SHUTDOWN   0
#define A210_SYSTEM_COLD_RESET 1

int a210_power_get_on(const struct device *dev, bool *on);
int a210_power_set_on(const struct device *dev, bool on);
int a210_system_request(const struct device *dev, uint32_t state);
int a210_power_sync_clock(const struct device *clock, unsigned int id);
#endif
