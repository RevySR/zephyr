/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_RESET_A210_H_
#define ZEPHYR_INCLUDE_DRIVERS_RESET_A210_H_

#include <zephyr/device.h>

int a210_reset_save_domain(const struct device *dev, unsigned int domain);
int a210_reset_restore_domain(const struct device *dev, unsigned int domain);

#endif
