/* SPDX-License-Identifier: Apache-2.0 */
#include "regulator_silergy.h"
#define DEFINE_OUTPUT(node) SILERGY_DEFINE(node, SILERGY_SY70209)
DT_FOREACH_STATUS_OKAY(silergy_sy70209_regulator, DEFINE_OUTPUT)
