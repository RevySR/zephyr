# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SCMI_SERVER base.c server.c smt.c)
zephyr_library_sources_ifdef(CONFIG_SCMI_SERVER_VOLTAGE voltage.c)
zephyr_library_sources_ifdef(CONFIG_SCMI_SERVER_REGULATOR regulator.c)

zephyr_library_sources_ifdef(CONFIG_SCMI_SERVER_PERFORMANCE performance.c)
zephyr_library_sources_ifdef(CONFIG_SCMI_SERVER_CLOCK clock.c)
