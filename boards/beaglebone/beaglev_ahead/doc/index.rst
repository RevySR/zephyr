BeagleV Ahead
=============

The ``beaglev_ahead/th1520/e902`` target runs on the E902 always-on processor.
It exposes SCMI Base and two Voltage Domain instances: CPU (ID 0) and AP
(ID 1). CPU couples DA9063 merged BCORE1/2 with BIO; AP uses DA9121.

The BeagleV-Ahead v1.0 schematic, sheets 13, 14 and 16, identifies the
PMIC control bus and rail connections. Both core buck outputs feed
DVDD08_CPU, while BIO feeds DVDD08_CPU_MEM. The schematic requires
-50 mV < CPU memory minus CPU core <= 300 mV. The configured 20 mV
step and core-minus-memory limit of 40 mV stay inside this bound.
The initial 0.8--1.0 V operating window, I2C addresses and settling
policy follow the common TH1520 AON configuration. Firmware checks
DA9063 identity, merged mode and active voltage bank before access.

This differs from the local LIGHT A debug configuration, which uses
separate BCORE1 and BCORE2 outputs for core and memory. The boards use
the same PMIC drivers; each describes its own power wiring.

See :zephyr:code-sample:`scmi_server` for the loader contract and validation
limits. Bus clocks, pinmux and PMIC setup must already be active, including
the DA9063 GPIO2 enable for DA9121. Hardware execution remains unverified.

Reference: https://github.com/beagleboard/beaglev-ahead/blob/main/BeagleV-Ahead_SCH.pdf
