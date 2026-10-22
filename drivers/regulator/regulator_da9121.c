/* SPDX-License-Identifier: Apache-2.0 */
#include "regulator_pmic_engine.h"
#define DEFINE_OUTPUT(node) PMIC_DEFINE(node, &pmic_da9121_driver)
DT_FOREACH_STATUS_OKAY(dlg_da9121_regulator, DEFINE_OUTPUT)
