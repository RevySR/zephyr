# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(regulator_pmic)
target_sources(testbinary PRIVATE main.c
    ${ZEPHYR_BASE}/drivers/regulator/pmic_backend.c
    ${ZEPHYR_BASE}/drivers/regulator/pmic_da9063.c
    ${ZEPHYR_BASE}/drivers/regulator/pmic_da9121.c
    ${ZEPHYR_BASE}/drivers/regulator/pmic_slg51000.c
    ${ZEPHYR_BASE}/drivers/regulator/coupled_backend.c
)
target_include_directories(testbinary PRIVATE ${ZEPHYR_BASE}/drivers/regulator)
