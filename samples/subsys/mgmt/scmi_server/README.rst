.. zephyr:code-sample:: scmi_server
   :name: SCMI platform server

   Serve SCMI platform management requests from an E902 processor.

Overview
********

This sample uses Zephyr devices for the mailbox, timebase, I2C bus and
regulators. The SCMI server lives in ``subsys/mgmt/scmi``. Platform
support is in the normal SoC, board and devicetree directories. The base
is Zephyr v4.4.2 (dccb09599635bdff17633fa7e9dab014b91dce90).

Supported build targets
***********************

.. list-table:: Current protocol exposure
   :header-rows: 1

   * - Board target
     - Voltage domains
   * - ``light_val_a/th1520/e902``
     - Coupled DA9063 BCORE1/BCORE2 and DA9121 AP
   * - ``beaglev_ahead/th1520/e902``
     - Coupled DA9063 merged cores/BIO and DA9121 AP
   * - ``milkv_meles/th1520/e902``
     - Coupled DA9063 merged cores/BIO and DA9121 AP
   * - ``licheepi_4a/th1520/e902``
     - Coupled DA9063 merged cores/BIO and DA9121 AP
   * - ``melonpi/a210/e902``
     - Twenty SY70209/SY70300 supply domains

Ahead and Meles each describe their board-local power wiring. Lichee Pi 4A
uses the Lichee Module 4A power description. LIGHT val A follows the published ``LIGHT A debug``
configuration; verify the populated board revision before using it.
PMIC model names are driver selections, not synthetic board targets.

Build
*****

A bare-metal RISC-V GCC with RV32E/ILP32E multilib support is required.
With the workspace Python environment and toolchain used for this port::

   export ZEPHYR_BASE="$PWD/zephyr"
   export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
   export CROSS_COMPILE=/usr/bin/riscv64-unknown-elf-
   cmake -S zephyr/samples/subsys/mgmt/scmi_server \
     -B build-native/milkv_meles -GNinja \
     -DBOARD=milkv_meles/th1520/e902 \
     -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
     -DSYSROOT_DIR=/usr/riscv64-unknown-elf
   cmake --build build-native/milkv_meles

The ``zephyr`` subdirectory in each build contains ``zephyr.elf``,
``zephyr.bin``, ``zephyr.map``, ``zephyr.dts`` and ``.config``.

Boot and transport contract
***************************

These raw images require a loader which has initialized the bus clock,
pinmux, PMICs and E902 timebase (3 MHz on TH1520, 24 MHz on A210). The polling I2C driver adopts the
inherited controller timing. It does not replace cold initialization.
The sample disables interrupts and polls the AP mailbox. It requires
exclusive ownership of that channel and its shared memory. The E902
mapping must be uncached; an AP with a cacheable mapping must perform
cache maintenance in addition to its ordering barriers.

TH1520 loads at 0xffef8c00 with a 52 KiB code/data/stack budget. Its
256-byte SCMI SMT area starts at 0xfff05c00. The image leaves the vendor
3 KiB configuration prefix and DDR scratch area outside its RAM region.
A210 loads at 0x308f8000 with a 63 KiB budget and a 256-byte SMT area at
0x30907c00. The linked entry point is the raw image entry.

Startup initializes SMT ownership before enabling reception. It must
not overwrite a channel still owned by a running vendor AON service.
The shared ``mbox_e902`` driver implements the register interface
shared by TH1520 and A210. Both reference SDKs select the same mailbox
driver, under ``csi/drivers/mbox/wj/v5.0``. RX/TX apertures and the receive status
bit belong to each SoC device tree. A210 uses the TH1520 register
interface as its compatible fallback. Mailbox channel 0 denotes the AP
doorbell, with no inline message data.
The client uses a single outstanding synchronous request and must not
reuse a timed-out channel until firmware or the peer recovers it.

SCMI Base 2.0, synchronous Voltage Domain 1.0 and Performance 1.0 are
implemented. A210 also provides Clock 2.0, Reset Domain 3.0, Power Domain
3.0, Sensor 2.0 and System Power 2.0. Voltage IDs follow each board's
``zephyr,user/voltage-domains`` order. Ahead, Meles,
Lichee Pi 4A and LIGHT val A expose CPU as ID 0 and AP as ID 1. Melon Pi
exposes CPU, CPU memory, CPU P, DDR, VP, NPU/VIP and GPU as IDs 0..6.
The remaining Melon Pi IDs expose fixed peripheral and DDR I/O rails.
These IDs are a new SCMI contract, not the vendor RPC numbering.

A210 platform resources
***********************

The Melon Pi sample exposes the resources used by the full A210 Linux DT:
163 clocks, 144 resets, thirteen power domains, five temperature sensors
and sixteen voltage-monitor channels. Clock, reset and power IDs are
specified in ``include/zephyr/dt-bindings/{clock,reset,power}/zhihe-a210.h``.

Clock IDs 0..9 remain the UART serial clocks, and IDs 10..19 their APB
clocks. IDs 20..22 are the PERI1..3 pad clocks, ID 23 is the AON system
clock, and IDs 24..25 are the AON I2C1 clocks. Likewise, reset IDs 0..9
are the UART0..9 serial resets and IDs 10..19 their APB resets; each is a
separate hardware line. Other resources extend these initial IDs. The backend supports explicit reset assertion/deassertion;
the AP must quiesce consumers before asserting a reset.

Clock rates come from hardware. Programmable clocks advertise their
supported rates, while CPU frequency changes use Performance domains.
The firmware owns parent selection and initializes the audio PLLs, eMMC
parent and HDMI pixel-clock parent before the AP uses SCMI. Linux requests
leaf rates without programming those parents independently.

The firmware retains both mailbox endpoint clocks and the UART4 console
clocks. Linux must not obtain the mailbox clocks through the same SCMI
transport, or write the AP clock/reset register words directly. Internal
power-domain clock holds are separate from the AP's enable votes. GPU
IOPMP clocks remain available while its state is restored after power loss.

Power domains sequence PCUs, supplies, clocks and resets. GPU and display
clock/reset state is restored on resume; display IOPMP settings are saved
and restored, and GPU permissions are re-established before consumer use.
The driver reconciles an on-state request if a shared PHY reset has removed
the domain's hardware state.

SCMI Sensor reports temperatures in millidegrees Celsius and voltages in
millivolts. Sensor configuration controls AP access to readings; the PVT
monitor continues sampling for firmware use. Notifications, trip points and
programmable sampling intervals are not advertised.

System Power implements forceful shutdown and cold reset. It does not
advertise warm reset, suspend or graceful notifications. This requires a
matching AP reset client; a boot chain still using the vendor AON RPC reset
handler does not become an SCMI client merely by loading this image.

CPU performance
***************

Performance domain 0 controls C910 on TH1520 or C908 on A210. A210 domain
1 controls C920. Levels are frequencies in kHz. Reported power costs are
abstract frequency-based values, not measured power in milliwatts.
The fixed OPP tables define the supported limits; notifications, fast
channels and writable limits are not implemented.

A210 supports 500, 1200, 1500, 1698 and 1896 MHz on C908, and 500, 1698
and 1896 MHz on C920. Higher C920 points need binned parts and are not
supported. The voltage tables follow the vendor SoC OPPs at kernel revision
``4625f5d54c2da015c8b0ae98b8a40b421ba6abe5``. C908 uses separate core and
SRAM supplies. Firmware raises SRAM before core, raises voltage before
frequency, and reverses that order when reducing performance. Every
voltage write is read back.

TH1520 supports sixteen levels from 300 through 1848 MHz. The configured
coupled supply remains at 800 mV through 1500 MHz and uses 1 V above that.
It retains each board's existing core/memory spread constraints.

A210 temporarily selects a 500 MHz GMAC-derived backup while retuning a
CPU PLL. TH1520 retunes the inactive CPU PLL. Lock waits are bounded and
failed transitions retain a working clock without reducing voltage.
A210 fabric dividers stay at or below the vendor OPP ceilings using the
inherited VIDEO/GMAC parents. TH1520 keeps its CPU bus at or below 750 MHz.

The AP must reserve the firmware-owned CPU clocks and shared divider
registers. Linux uses SCMI Performance domains through the standard
SCMI CPUFreq driver. CPU supplies remain readable through Voltage, but
independent voltage changes and power-off requests are denied. Confirming
an already-active supply succeeds without changing hardware. Firmware
startup requires initialized PLLs and sufficient inherited CPU voltage;
it is not a cold CPU clock or PMIC initialization sequence.

Power behavior
**************

The PMIC providers separate I2C/timing and Zephyr device adaptation from
chip register maps. Each output allocates only its selected backend state.
The providers and coupled controller require ``CONFIG_MULTITHREADING=n``
because a register read/modify/write and a multi-rail ramp must not
interleave with another consumer. The sample is the sole owner of the
adopted regulator enable references; sharing those references with other
consumers or accessing PMICs from interrupt callbacks is unsupported.

Regulator initialization observes hardware without enabling outputs or
clamping their voltages. The optional ``regulator_get_enabled()`` API
reports the physical enable state and I/O failures. Existing software
reference-count semantics in ``regulator_is_enabled()`` are unchanged.
The SCMI adapter rejects nonlinear voltage lists and retains failed
providers as unavailable domains rather than shifting other domain IDs.

DA9063 checks identity, merged-output mode, software control and active
voltage bank. DA9121 checks identity/revision and active VSEL bank.
SLG51000 checks fuse limits, selected range and bypass mode.
The supported PMIC set follows the selected boards: DA9063/DA9121 and
SLG51000 for the LIGHT A reference design, and SY70209/SY70300 for A210.

SY70209/SY70300 preserve shared enable bits and observe the SY70209 GPIO2
parent supply without toggling it. Writes include bounded settling and
readback. Failed writes are reported as indeterminate; they are not
silently retried. The coupled regulator plans all steps before writes,
checks every rail after each step and latches a fault on an uncertain
transition. Its delta is core minus memory: -300 mV through +40 mV.
The +40 mV limit keeps the Ahead and Meles schematics' strict +50 mV bound
on the 20 mV BIO selector grid. LIGHT val A uses the same conservative bound
and a 10 mV grid. Group disable is denied.

Validation and remaining work
*****************************

The five targets have been compiled and linked. Host tests exercise
SCMI framing, SMT ownership, the polling AP client, I2C error paths,
PMIC selectors and coupled ramps. Native-simulator tests cover hardware
status error propagation, the Zephyr regulator adapter, I2C register
encoding, partial-read failures, invalid timebases and pending mailbox
notifications across enable/disable. The mailbox probe clears stale
notifications before SMT publication; subsequent enable operations
preserve messages that are already pending. The synthetic
``tests/drivers/regulator/pmic_build`` overlay provides compile coverage
for PMIC families absent from the selected boards; do not run that
synthetic configuration on hardware.

Melon Pi V2/V3 has booted Linux v7.3 with this E902 firmware. SCMI voltage
readback, accelerator supply cycling and every CPU OPP have been exercised.
CPU frequencies were checked with the hardware clock monitor, CPU supply
voltages through SCMI readback, and execution with per-core memory and
integer checksums. A 40-second run completed 1206 concurrent cluster
transitions with all eight CPU checksums passing. CPU1 through CPU7 were
offlined and brought back at 500 MHz. These checks do not establish long-term voltage margin
or characterize every silicon sample.

TH1520 CPU DVFS has build and register-model coverage; physical execution
on LIGHT val A, Ahead, Meles and Lichee Pi 4A remains untested. The initial
A210 Clock/Reset services have build and protocol tests; their hardware
validation is recorded separately from the earlier DVFS results.
Remaining peripheral clocks/resets, Power Domain, Sensor, System Power,
watchdog and suspend services remain unimplemented. The raw images have neither vendor
AON_CONFIG packaging nor vendor ready magic and RPC compatibility; they
are not drop-in loader replacements.
The RWX SRAM load segment is expected without a PMP policy.

References
**********

* TH1520 reference: https://github.com/revyos/aon at
  ``2b12bddfa49d4c83d9537ad47e39a78d1e8adeea``. Register mappings and
  LIGHT A wiring follow the PMIC sources and ``aon_pmic.c`` there.
* A210 reference: local p100-img-aon SDK at
  ``569e091d9a4dce8b366009745f7f2e155f7eb0b0``; board constraints follow
  ``arch/riscv/boot/dts/zhihe/a210-som.dtsi`` at
  ``013faac16cda0b6748b697b14979f4fab8725c43``.
* Ahead schematic: https://github.com/beagleboard/beaglev-ahead,
  ``BeagleV-Ahead_SCH.pdf``, v1.0, sheets 13, 14 and 16. BCORE1/2 are
  merged for CPU, BIO supplies CPU memory, and DA9121 supplies AP.
* Meles schematic: https://github.com/milkv-meles/meles-files,
  ``hardware/meles-v1.2-schematic.pdf``, page 22.
* SCMI behavior was cross-checked against ARM SCP-firmware and Linux
  ``drivers/firmware/arm_scmi/voltage.c``. Register facts were checked
  against Linux DA9063, DA9121 and SLG51000 drivers. The implementation
  uses no linked vendor RTOS code.

The workspace ``validation/sources.json`` pins source and reference
hashes. Build logs and the final artifact manifest are kept alongside it.

.. toctree::
   :maxdepth: 1

   services
