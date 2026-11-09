---
name: m01-firmware-project
description: User wants a full replacement firmware for the MDP-M01 display unit; hardware map and plan live in notes/m01-hardware.md
metadata:
  node_type: memory
  type: project
  originSessionId: a76b5cbf-6224-4158-99a9-160621013d7c
  modified: 2026-11-09T13:59:52.199Z
---

On 2026-11-09 the user chose to write **new M01 firmware from scratch**. They dislike everything about the stock UI: navigation, main-screen layout, look, and missing features. A PC control panel or binary patching were rejected.

The user's M01 is PCB V1.3 with a **GD32F303VET6** (512 KB flash / 64 KB RAM). The full pin map, flash layout and radio protocol are in `notes/m01-hardware.md` in the repo. Prior art used as the base:
- github.com/Nikolas-S/OpenMDP-RE (GPL-3.0): schematic, pins. **SWD is on micro-USB D+ (SWDIO) / D− (SWCLK)**.
- github.com/leommxj/mdp_commander (Unlicense): the full M01↔P906 nRF24 packet protocol.

2026-11-09: the full 512 KB M01 flash was backed up over SWD (`dump/m01_flash_1/2.bin`, identical). The user's rig is a USB-A socket wired to a clone ST-Link, with the M01 plugged in via its normal cable. CubeProgrammer `mode=HOTPLUG freq=480` works. The unit was running **M01 V1.34.098** (bootloader DFU V3.65D), not v2.03.

Firmware lives in `m01fw/` (bare-metal C, CMSIS, CMake+Ninja, Arm GNU Toolchain 12.2 installed via winget). Milestone 1 (bring-up screen) was flashed 2026-11-09. Gotchas:
- The bootloader only jumps if `(SP & 0x2FFF0000) == 0x20000000`, so `_estack` must be < 0x20010000.
- Flash with CubeProgrammer `mode=NORMAL reset=SWrst -d build/m01fw.hex -v -rst`.
- Read the `dbg` struct in RAM with `-r32` in HOTPLUG mode to check state without seeing the screen.

**Requirement (user, 2026-11-09): preserve the pairing workflow.** The new firmware must pair with the P906 the same way stock does: Auto Match via radio type 5/6 packets on channel 78, and the address/channel stored in `RX_ADDR.TXT`, multi-device Addr1..N. Already-paired units must keep working without re-pairing. The new firmware must also be able to **re-pair an unpaired P906** by itself, reachable from the UI and from a key held at boot. Fallbacks: restore stock firmware from `dump/m01_flash_1.bin`, or pair with mdp_commander.

**Why:** a multi-session project; avoids redoing the pin/peripheral recon.
**How to apply:** keep the stock bootloader (app at 0x08008000) so DFU (hold A + plug USB) can always recover. Back up the M01 flash over SWD before flashing anything. Next open steps: confirm the MCU and find the SWD pads (needs board photos or ST-Link), then reverse-engineer the radio protocol using both firmwares. Related: [[p906-recovery]], [[p906-firmware-findings]].
