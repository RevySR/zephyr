# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(scmi_platform)
target_compile_definitions(testbinary PRIVATE CONFIG_SCMI_SERVER_POWER=1
  CONFIG_SCMI_SERVER_CLOCK=1 CONFIG_SCMI_SERVER_RESET=1
  CONFIG_SCMI_SERVER_VOLTAGE=1 CONFIG_SCMI_SERVER_PERFORMANCE=1
  CONFIG_SCMI_SERVER_SENSOR=1 CONFIG_SCMI_SERVER_SYSTEM_POWER=1)
target_sources(testbinary PRIVATE main.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/power.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/clock.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/reset.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/sensor.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/system.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/voltage.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/performance.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/base.c
  ${ZEPHYR_BASE}/subsys/mgmt/scmi/server.c)
