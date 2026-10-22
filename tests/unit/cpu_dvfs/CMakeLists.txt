# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu_dvfs)
target_sources(testbinary PRIVATE main.c ${ZEPHYR_BASE}/drivers/clock_control/cpu_dvfs.c)
target_include_directories(testbinary PRIVATE ${ZEPHYR_BASE}/drivers/clock_control)
