---
name: p906-recovery
description: "How the user's MDP-P906 was recovered from a DFU-only soft brick via ST-Link, how SWD must be powered, and the flash layout"
metadata:
  node_type: memory
  type: project
  originSessionId: f2752340-1e00-4e4e-8977-598ba3c7fb12
  modified: 2026-11-09T12:57:54.944Z
---

On 2026-11-09 the user's Miniware MDP-P906 (board P906-DIG-2.2, STM32F103VCT6, RDP off) was stuck booting into DFU. Cause: the bootloader's virtual DFU drive had written the firmware 0x1C00 bytes too low, so there was no valid vector table at 0x08008000. Fix: programmed MDP_P906_v3.01.hex directly with a clone ST-Link V2 + STM32CubeProgrammer CLI (`-c port=SWD mode=UR reset=HWrst -d <hex> -v`); it boots normally now.

SWD only worked once the STM32 was actually powered: the user wired the ST-Link's 3.3 V output to the VCC pin of the W25Q64 SPI flash (same 3.3 V rail). Powering the unit over its USB-C port does NOT power the MCU rail in that state. Earlier "Unable to get core ID" failures and the ~2 V VDD reading were the chip being phantom-powered through the SWD pins, not wiring or firmware issues.

Flash layout: bootloader 0x08000000–0x08006FFF ("BDFU V3_42_D"), firmware at 0x08008000, calibration/settings pages 0x0803D000–0x0803F7FF (leave untouched). Full backups are in `dump\` (p906_flash_1/2.bin = before, p906_flash_after.bin = after).

**Why:** future SWD work on this unit fails silently unless the 3.3 V rail is fed; the DFU drive is unreliable for .bin updates; the user's laptop can't enumerate the P906 in DFU mode.
**How to apply:** for SWD, feed 3.3 V to the W25Q64 VCC, connect GND/SWDIO/SWCLK/NRST, connect under reset; erase only firmware pages, never the full chip. For USB updates use .hex only in a fresh DFU session. See [[p906-firmware-findings]].
