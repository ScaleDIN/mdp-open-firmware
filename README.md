# MDP-P906 recovery and firmware notes

Notes, backups and tools from recovering a Miniware **MDP-P906** (board `P906-DIG-2.2`, STM32F103VCT6) from a DFU-only soft brick, plus reverse-engineering notes on its bootloader and v3.01 firmware.

## Contents

| Path | What it is |
|---|---|
| `MDP_P906_v3.01.zip`, `MDP_M01_v2.03.zip` | Official Miniware firmware packages, as downloaded |
| `backup/` | Files from the P906's `P906_APP` drive before it was formatted (`*.CFG`/`*.PRM` calibration/config, `Q_SET.TXT`, `RX_ADDR.TXT`) plus `MDP_P906_v1.33.bin` |
| `dump/p906_flash_1.bin`, `dump/p906_flash_2.bin` | Full 256 KB internal flash **before** the fix (two identical reads) |
| `dump/p906_flash_after.bin` | Full 256 KB flash **after** the fix: bootloader + v3.01 + calibration. **Known-good image for this unit.** |
| `patched/MDP_P906_v3.01_pdfix.hex` | USB-PD charger-compatibility patch, built by `tools/pdfix.py` (see below). **Untested on hardware.** |
| `patched/MDP_P906_v3.01_pd28v.hex` | Experimental 28 V USB-PD patch. **Not flashed and not recommended** (see below). |
| `tools/` | Python analysis scripts (capstone-based disassembly, dump analysis, hex patch + verify) |
| `notes/claude-memory/` | Condensed findings kept as project memory |
| `photos/` | Board photos (P906 MCU area, USB-C input area, P906 DC/DC board, M01 mainboard V1.3) |
| `dump/m01_flash_1.bin`, `dump/m01_flash_2.bin` | Full 512 KB flash of the **MDP-M01** (GD32F303VET6) read over SWD: bootloader DFU V3.65D, firmware V1.34.098, settings, logo. Two identical reads. |
| `m01fw/` | Open replacement firmware for the M01 (work in progress). Hardware map and findings are in `notes/m01-hardware.md`. |

Not included: the MDP PC software (`MDPUpperV1.2.zip`, 135 MB) and the STM32CubeProgrammer installer. Both are too large for GitHub and come from their vendors.

## What went wrong

- The DFU update drive wrote firmware images **0x1C00 bytes (14 sectors) too low**. `0x08008000` held mid-image text and no vector table, so the bootloader stayed in DFU on every boot.
- Every attempt still reported `.rdy`, because the bootloader never checks the image it wrote.
- Formatting the `P906_APP` drive was **not** the cause. The unit booted normally after the format.

## How it was fixed (SWD / ST-Link)

1. **Power the MCU properly.** The USB-C port does not power the STM32's 3.3 V rail in this state. Feed the ST-Link's **3.3 V output to the VCC pin of the W25Q64 SPI flash**, which is on the same 3.3 V rail. Without this, SWD fails with "Unable to get core ID", and VDD reads around 2 V because the chip is phantom-powered through the SWD pins.
2. Connect GND, SWDIO (PA13, pin 72), SWCLK (PA14, pin 76) and NRST (pin 14).
3. Read protection (RDP) is **off**. Back up first:
   `STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -u 0x08000000 0x40000 backup.bin`
4. Program only the firmware area. The `.hex` carries its own addresses, so only pages 0x08008000–0x0802D7FF are erased:
   `STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -d MDP_P906_v3.01.hex -v`
5. Release with `-hardRst`.

To restore this exact unit (bootloader, firmware and calibration), write `dump/p906_flash_after.bin` at `0x08000000`.

## Flash layout

| Range | Contents |
|---|---|
| `0x08000000–0x08006FFF` | Miniware bootloader (`P906_DFU`, `BDFU V3_42_D`, "Virtual DFU Disk") |
| `0x08008000–0x0802D0E7` | Application (v3.01) |
| `0x0803D000–0x0803F7FF` | Calibration / settings pages. **Do not erase.** |

## Bootloader DFU drive: safe USB update procedure

The DFU drive is a FAT12 disk emulated in RAM. Pre-filled dummy entries take clusters 2–14, so the first free data sector is at byte `0x6A00`.

- **`.hex`**: lines are parsed in order and must start with `:` and end in **CRLF**. Line addresses are absolute, so the image can't be shifted.
- **`.ADR`** (then `.bin`): the address is checked (≥ `0x08008000`) and the session switches to bin mode. The bin's start is then **guessed** from fixed thresholds (`0x6C00` right after ADR, otherwise `0x8C00`). The directory entry's real start cluster is ignored. Leftover files on the drive push the `.bin` further along and shift the whole image. That's what bricked this unit.

**Safe procedure:**
1. Power off fully, then enter DFU (hold SET, press MENU/RUN). This gives a fresh session.
2. Copy **only** the `.hex` from the original zip. Don't copy `.ADR`, and don't format, delete or rename anything on the drive.
3. Wait for `.rdy`. `.ERR` or `.NOT` means the update failed: start over.
4. Power-cycle, then check the version on the device info screen. `.rdy` alone proves nothing.

## Firmware findings (v3.01)

- **USB-PD**: understands fixed, PPS and EPR (AVS) offers. It requests 5 V, then the highest voltage up to **20 V**. It enters EPR mode but still requests 20 V (`0x0801ED92: movw r0, #20000`).
- **Input current limit**: a DAC-driven hardware limiter set to the negotiated PD current, minus a small margin. "Unlimit" means "the charger's advertised current", not truly unlimited. Anything above **3 A needs a 5 A e-marked cable**. Confirmed: MAX 5.00 A with a strong charger and a good cable, so 100 W in.
- **Current Limit Mode** (Normal / Unlimit) only toggles GPIO **PE5**.
- **28 V patch** (`patched/`): a one-instruction change, 20000 → 28000 mV. **Not flashed.** The USB-C input switch is an **AON7409** (−30 V VDS, ±25 V VGS). That leaves too little margin at 28 V, and the gate may see the full input voltage. The K34 diode is a 40 V Schottky and isn't the issue. Treat **20 V × 5 A** as the USB-C ceiling, and use the 30 V barrel jack for more power.

## USB-PD charger compatibility patch (`patched/MDP_P906_v3.01_pdfix.hex`)

Some USB-C chargers never trigger 20 V on stock v3.01. There are two main causes:

1. **Attach detection.** The firmware only treats a charger as connected if its CC pull-up advertises at least 1.5 A (`cmp r0,#2` on FUSB302 BC_LVL at `0x080219F8`). Chargers that advertise "Default USB" current fail with `fusb3022 CC1 or CC2 error`, and PD never starts.
2. **Missing protocol replies.** The receive handler (`0x0801EBC0`) only reacts to Accept, PS_RDY, Source_Capabilities and the EPR messages. Soft_Reset, Get_Sink_Cap, VDM requests and other PD 3.0 requests get no answer. Strict chargers respond with Soft_Reset and then Hard_Reset. A Hard_Reset cuts VBUS and reboots the unit, and some chargers then give up. Reject and Wait are also ignored.

Smaller issues: the FUSB302 never retries a transmission (`Control3 = 0x06`). The Request is sent from the main loop, so it can miss the charger's ~30 ms deadline. Only `Interrupt` is cleared in the ISR, so a latched `InterruptA`/`InterruptB` bit could hold INT_N low.

`tools/pdfix.py MDP_P906_v3.01.hex out.hex` makes these changes. It checks the original bytes before writing anything.

| Where | Change |
|---|---|
| `0x080219F8` | CC detect accepts BC_LVL ≥ 1, so any Rp works |
| `0x08021A16` | FUSB302 `Control3 = 0x07`: hardware auto-retry, 3 retries |
| `0x0801EC7E` | Receive dispatcher hooked to new code. It answers **Soft_Reset** (Accept, reset MessageID, renegotiate) and **Get_Sink_Cap** (5 V 3 A + 20 V 5 A). It sends **Reject** for swaps. For Get_Source_Cap, Data_Reset, PD 3 requests and VDM requests it sends **Not_Supported** (on PD 2.0 sources: Reject, or nothing for unknown and VDM messages). After **Wait** it retries the same request after ~150 ms. After **Reject** it clears the stuck "request pending" flag. Everything else goes to the original handler. |
| `0x08021966` | FUSB302 EXTI hooked: the Request is sent straight from the interrupt about 1 ms after Source_Capabilities, and `InterruptA`/`InterruptB` are cleared |
| `0x0801EE94` | The main-loop PD tick runs with EXTI15_10 (IRQ40) masked, so the interrupt can't break a bit-banged I2C transfer |
| `0x0802D100–0x0802D2B3` | New code (436 bytes) in unused flash after the app |

Not changed: the first Source_Capabilities after boot still gets no GoodCRC, because AUTO_CRC is only turned on after it arrives, so the charger has to resend it. PPS is still preferred over fixed 20 V when a charger offers PPS up to 20 V or more. Hard_Reset is still not detected; a Hard_Reset drops VBUS, so the unit reboots anyway.

Flash it over SWD as in the recovery section. The `.hex` erases only app pages, and calibration at `0x0803D000` is untouched:
`STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -d patched/MDP_P906_v3.01_pdfix.hex -v`
To undo it, flash the stock `MDP_P906_v3.01.hex` (or `dump/p906_flash_after.bin` at `0x08000000`).

