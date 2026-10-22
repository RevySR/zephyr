/* SPDX-License-Identifier: Apache-2.0 */
#include "regulator_pmic_engine.h"
#define DEFINE_OUTPUT(node) PMIC_DEFINE(node, &pmic_slg51000_driver)
DT_FOREACH_STATUS_OKAY(dlg_slg51000_regulator, DEFINE_OUTPUT)
