/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_REGULATOR_PMIC_OUTPUT_H_
#define ZEPHYR_DRIVERS_REGULATOR_PMIC_OUTPUT_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Private register-engine interface; Zephyr devices expose regulator_driver_api. */
struct pmic_output_api {
	int (*get_level)(void *context, int32_t *uv);
	int (*set_level)(void *context, int32_t uv);
	int (*get_enabled)(void *context, bool *enabled);
	int (*set_enabled)(void *context, bool enabled);
};
struct pmic_output {
	const char *name;
	int32_t min_uv, max_uv, step_uv;
	bool always_on;
	const struct pmic_output_api *ops;
	void *context;
};
#endif
