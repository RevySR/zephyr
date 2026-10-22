/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_SY70209_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_SY70209_H_

/* Buck outputs 1..8; LDO outputs 1..5 and 7..10 (there is no LDO6). */
#define SY70209_BUCK(n) (n)
#define SY70209_LDO(n)  (0x10 + (n))

#endif
