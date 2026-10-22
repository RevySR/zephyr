XuanTie SCMI firmware scope
==========================

The target is a Zephyr implementation of the platform-management duties
currently performed by the TH1520 and A210 vendor AON firmware, with SCMI
as the application-processor interface. AON names describe the execution
and power domain; they do not require the vendor RPC protocol. The
``mbox_e902`` driver is a hardware transport, independent of SCMI services.

Current coverage
****************

Base, Voltage and CPU Performance services are implemented for the supported
boards. A210 additionally implements the full bring-up DT's Clock, Reset,
Power Domain and Sensor resources, plus System Power shutdown/cold reset.
The inventory below distinguishes these services from the remaining vendor
firmware responsibilities. TH1520 peripheral management and complete cold
initialization are not covered by the A210 migration.

.. list-table:: Service ownership and remaining work
   :header-rows: 1
   :widths: 27 35 38

   * - Vendor responsibility
     - Intended owner/interface
     - Current status
   * - PM voltage get/set and rail enable get/set
     - SCMI Voltage with Zephyr regulators
     - Implemented; A210 voltage readback and accelerator supply cycling tested
   * - PM operating modes and PMIC fault reporting
     - Regulator policy and explicit status/error mapping
     - Missing; mode control is not equivalent to a voltage request
   * - PM temperature and PVT readings
     - SCMI Sensor with device drivers
     - Implemented on A210: five temperatures and sixteen voltage channels
   * - Clock and coordinated DVFS/AVFS policy
     - SCMI Clock and Performance with SoC clock/voltage sequencing
     - CPU Performance implemented; A210 OPPs tested, TH1520 build/model only.
       All 163 clocks used by the A210 bring-up DT are exposed. Adaptive
       voltage policy and TH1520 peripheral clocks remain unimplemented
   * - Device power and reset sequencing
     - SCMI Power Domain and Reset Domain with SoC providers
     - Implemented on A210: 144 resets and thirteen power domains
   * - Watchdog restart and power-off requests
     - SCMI System Power with platform shutdown/reset sequences
     - A210 provider and matching OpenSBI client implemented; paired
       shutdown and reset validation remain pending
   * - Watchdog start/stop/ping/timeout/state and AON self-monitoring
     - Firmware watchdog policy; review the AP control contract separately
     - Missing; no direct mapping in the selected standard service set
   * - LPM suspend-to-RAM and resume completion
     - SCMI System Power plus retention, wake sources and resume providers
     - Missing
   * - LPM CPU hotplug
     - OpenSBI HSM for the OS; SCMI Power Domain for firmware coordination
     - A210 OpenSBI HSM hotplug tested. Physical CPU power removal through
       the management firmware remains unimplemented
   * - SYS reserved SRAM and MISC register-dump setup
     - DT/boot handoff and bounded diagnostics
     - Missing; do not expose arbitrary register writes as a standard service
   * - PMIC, clock, bus, wakeup and memory initialization
     - Zephyr SoC/board startup and loader contract
     - The sample adopts initialized hardware. Existing boot loaders must
       transfer clock, reset and PMIC ownership before using SCMI

The earlier A210 functional results used the 98-reset firmware with the
legacy boot chain. The 109-reset firmware is build/model checked and its
initialization was observed through the new coreboot handoff; full runtime
validation of that paired chain remains pending.

The TH1520 reference registers PM, MISC, SYS, WDG and LPM dispatchers.
Its MISC implementation handles register-dump configuration, while the
SET_CONTROL/GET_CONTROL identifiers are declared but have no handler in
that dispatcher. AVFS is a service identifier but is not a registered
service in this solution's startup. A210 additionally initializes droop
monitoring and SPI support. Header enums alone are not evidence that a
vendor command is implemented.

Replacement contract
********************

Standard SCMI services should represent the resource operations above.
A vendor extension is only appropriate for a remaining AP-visible
operation whose semantics cannot be represented by the selected standard
protocols. Autonomous watchdog and protection policies may remain
internal firmware functions. They still belong in the parity inventory.

The transport must support the completion and notification requirements
of the added protocols. The present synchronous, single-channel loop does
not yet provide asynchronous suspend completion or a notification channel.
The current serialized PMIC implementation is also not a multithreaded
platform service scheduler.

Complete replacement requires compatible AP clients, stable resource IDs,
cold initialization, watchdog ownership, reset/shutdown and suspend/resume
recovery, in addition to protocol framing. Existing loaders and clients
expecting vendor RPC or AON_CONFIG packaging must be migrated explicitly.
No legacy ready magic or successful build can establish this parity.

References
**********

* Arm SCMI 3.1: https://documentation-service.arm.com/static/6231acfc8804d00769e9db69
* TH1520 reference: https://github.com/revyos/aon at
  ``2b12bddfa49d4c83d9537ad47e39a78d1e8adeea``.
* A210 reference: local SDK at
  ``569e091d9a4dce8b366009745f7f2e155f7eb0b0``.
* Both source inventories use ``csi-rtos/solutions/fm_aon_low_power/src/``:
  ``main.c``, ``inc/aon_*.h`` and the corresponding service dispatchers.
