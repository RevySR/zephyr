/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_MBOX_E902_H_
#define ZEPHYR_DRIVERS_MBOX_E902_H_
#include <zephyr/device.h>
/** Dispatch a TH1520/A210 AP doorbell from its E902-side mailbox instance. */
int mbox_e902_poll(const struct device *dev);
#endif
