# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(regulator_silergy)
target_sources(testbinary PRIVATE main.c
    ${ZEPHYR_BASE}/subsys/mgmt/scmi/base.c
    ${ZEPHYR_BASE}/subsys/mgmt/scmi/server.c
    ${ZEPHYR_BASE}/subsys/mgmt/scmi/voltage.c
    ${ZEPHYR_BASE}/subsys/mgmt/scmi/smt.c
    ${ZEPHYR_BASE}/drivers/regulator/silergy_backend.c
)
target_include_directories(testbinary PRIVATE ${ZEPHYR_BASE}/drivers/regulator)

target_compile_definitions(testbinary PRIVATE CONFIG_SCMI_SERVER_VOLTAGE=1)
