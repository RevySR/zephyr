# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pmic_transport)
target_sources(app PRIVATE main.c ${ZEPHYR_BASE}/drivers/regulator/regulator_pmic.c)
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/drivers/regulator)
