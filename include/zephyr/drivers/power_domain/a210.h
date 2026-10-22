/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_POWER_DOMAIN_A210_H_
#define ZEPHYR_INCLUDE_DRIVERS_POWER_DOMAIN_A210_H_
#include <zephyr/device.h>
int a210_power_get_on(const struct device *dev, bool *on);
int a210_power_set_on(const struct device *dev, bool on);
int a210_power_sync_clock(const struct device *clock, unsigned int id);
#endif
