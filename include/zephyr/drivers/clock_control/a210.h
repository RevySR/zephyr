/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_A210_H_
#define ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_A210_H_

#include <zephyr/device.h>

int a210_clock_rate_count(const struct device *dev, unsigned int id);
int a210_clock_describe(const struct device *dev, unsigned int id, unsigned int index,
			uint32_t *rate);
int a210_clock_save_domain(const struct device *dev, unsigned int domain);
int a210_clock_restore_domain(const struct device *dev, unsigned int domain);
int a210_clock_hold(const struct device *dev, unsigned int id, bool hold);

#endif
