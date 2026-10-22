/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR75203_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR75203_H_
#include <zephyr/device.h>
int mr75203_channel_count(const struct device *dev);
int mr75203_temperature_count(const struct device *dev);
int mr75203_read_channel(const struct device *dev, unsigned int id, int32_t *milli);
#endif
