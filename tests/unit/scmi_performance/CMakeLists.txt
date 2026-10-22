# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(scmi_performance)
target_compile_definitions(testbinary PRIVATE CONFIG_SCMI_SERVER_PERFORMANCE=1
                                              CONFIG_SCMI_SERVER_VOLTAGE=1)
target_sources(testbinary PRIVATE main.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/performance.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/voltage.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/base.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/server.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/smt.c)
