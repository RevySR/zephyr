/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_REGULATOR_COUPLED_BACKEND_H_
#define ZEPHYR_DRIVERS_REGULATOR_COUPLED_BACKEND_H_
#include "pmic_output.h"
#define AON_COUPLED_MAX_RAILS 3
struct regulator_coupling {
	uint8_t a, b;
	int32_t min_delta_uv, max_delta_uv; /* voltage[a] - voltage[b] */
};
struct regulator_coupled_group {
	const struct pmic_output *rails[AON_COUPLED_MAX_RAILS];
	size_t count;
	const struct regulator_coupling *constraints;
	size_t constraint_count;
	bool faulted;
};
extern const struct pmic_output_api regulator_coupled_ops;
#endif
