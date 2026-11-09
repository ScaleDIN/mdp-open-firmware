# MDP-M01 hardware map

Sources:
- static analysis of `MDP_M01_v2.03.001.bin` (load address 0x08008000, 205 715 bytes, "M01 V2.03.001")
- photo of this unit (`photos/m01-mainboard-v1.3.jpeg`)
- [Nikolas-S/OpenMDP-RE](https://github.com/Nikolas-S/OpenMDP-RE) (GPL-3.0): schematic, pinout, boot notes
- [leommxj/mdp_commander](https://github.com/leommxj/mdp_commander) (Unlicense): radio protocol

## This unit
- PCB **MDP-M01-V1.3**. MCU **GigaDevice GD32F303VET6**: Cortex-M4, up to 120 MHz, 512 KB flash, 64 KB RAM. It is mostly register-compatible with STM32F103, so the stock firmware is one binary for both STM32 and GD32 boards.
- Radio is an **Si24R1** module (nRF24L01+ clone) with an external antenna wire. SPI flash is a **W25Q64JV**. The LCD is a **Z280IT002** (ILI9341, 240×320, 16-bit 8080 bus on FSMC).
- The stock code is StdPeriph + **LVGL** + FatFs.

## Backup of this unit (2026-11-09)
`dump/m01_flash_1.bin` = `dump/m01_flash_2.bin`, full 512 KB read over SWD through the micro-USB port. The two reads are identical, SHA-256 `946b4d99d85b81588ea0a7c6917de5efdcac0bc720d8712a5c5c8db0fe0afe90`. **Known-good image for this unit.**
- SWD: CubeProgrammer `-c port=SWD freq=480 mode=HOTPLUG` works with the app running. It reports device ID 0x414 "STM32F101/F103 High-density", 512 KB.
- Bootloader **DFU V3.65D**; its strings call the chip "GD32F103VE".
- Installed app: **M01 V1.34.098** (ends 0x08028156, 131 KB). This is *not* v2.03, and it has no LVGL; v2.03 is the newer LVGL-based UI.
- 0x0803C000: 74-byte settings block. 0x0803D800: boot logo BMP (+ more up to 0x0803F9FF). Everything from 0x08040000 up is erased, so 256 KB is free for us.

## Flash layout (stock)
| Range | Contents |
|---|---|
| 0x08000000–0x08007FFF | Miniware bootloader (32 KB, not public: **dump before writing anything**) |
| 0x08008000– | Application |
| 0x0803D800 (8310 bytes) | Boot logo, 256×64 4-bit BMP. Not part of firmware updates |
| higher pages | Settings (language, LED colour, grid/menu positions, RX IDs: `*_From_STMFlash`) |

Bootloader V3.65D jump condition (0x080062D8): key A (PE6) not held **and** `(*(u32*)0x08008000 & 0x2FFF0000) == 0x20000000`. So the initial SP must be **below 0x20010000**; top-of-64K-RAM fails, and the unit silently sits in DFU. There's no checksum.
Flashing over SWD needs `mode=NORMAL reset=SWrst`; erasing in `mode=HOTPLUG` fails while the app runs. Hold **A** while powering up to enter DFU.

## SWD
- **SWDIO = USB D+, SWCLK = USB D−** (PA13/PA14 are shared with the micro-USB). An ST-Link can connect through a micro-USB breakout without opening the case. RDP is off.
- Custom firmware **must leave SWD enabled** (JTAG-disable only). DFU mode also enables SWD, so that's the fallback. BOOT0 is hard-wired to GND.
- ST-Link GDB server refuses GD32 ("Could not verify ST device"). CubeProgrammer read/write works. For debugging, use OpenOCD with a gd32f3x config (CPUTAPID 0x2ba01477).

## Pins
| Function | Pins |
|---|---|
| LCD (FSMC) | D0–D15 on PD14,PD15,PD0,PD1,PE7–PE15,PD8–PD10; RS = A18 (PD13); NOE PD4; NWE PD5; NE1 PD7; RESET PB1 (active low); **backlight PB11** (active high) |
| Si24R1 radio | SPI1 remapped: SCK PB3, MISO PB4, MOSI PB5; CSN PA15; CE PD2; IRQ PD6 |
| W25Q64 | SPI2: CS PB12, SCK PB13, MISO PB14, MOSI PB15 |
| Keys A–E | PE6, PE5, PE4, PE3, PE2 (pull-up, active low) |
| Encoder "menu" | A PC15, B PC13 |
| Encoder "value" | A PC0, B PC1 |
| LED | blue PB6, red PB8 (active low, TIM4 PWM) |
| Buzzer | PB9 (active high, TIM4 CH4) |
| USB | PA11/PA12, presence PA10. Stock = CDC + MSC composite, VID 0416 PID DC01 |
| Misc | PD11 peripheral power enable (v1.21 only; no effect on v1.3); TP_PC10, TP_PC11 test points; PB2 tied to GND |

The encoders (Kailh EN8080) have an odd waveform: A and B change together on one edge and are skewed on the other, and they bounce. A standard quadrature decoder won't do; see OpenMDP-RE. Measured on this unit (2026-11-09):
- The "together" edge is the **common contact** making or breaking: A and B flip within 1 µs. It carries no direction information at any sample rate.
- The skewed edge passes through A=0,B=1 for **0.25–20 ms**, shorter when turning fast.
- Contacts bounce at µs scale (18k edges in about 30 s of turning), and B can chatter for 200 ms at rest.
- A fixed skew threshold therefore fails. The firmware compares each detent change with the previous one: the longer of the two is the skewed edge. See `m01fw/src/input.c`. The capture and replay tools are in `m01fw/tools/` (`elog.py`, `encsim2.py`).

## Radio protocol (M01 ↔ P906)
From mdp_commander (to be cross-checked against both firmwares):
- 5-byte address, 2 Mbps, CRC16, auto-ack, fixed 32-byte payloads. Pairing listens on channel 78.
- Frame: `type, len, payload…, xor` (XOR checksum seeded with 0x88), payload ≤ 28 bytes.
- Types:
  - 4: get V/I
  - 5: call for ID
  - 6: dispatch address/channel (pairing)
  - 7: status poll and set: V (`03 03` + BCD), I (`02 03` + BCD), output (`0C 00 on`). Returns input V/I, temperature, CC/CV state, lock and error flags, and realtime ADC samples.
  - 8: fast ADC sample burst (needs calibration constants)
  - 9: LED colour and device info
- Numbers are packed BCD (`12.345` → `01 23 45`).
- Pairing info is in `RX_ADDR.TXT` on the M01 drive: `Addr1=b1:61:a:29:21`, channel 16.

Verified with our firmware (2026-11-09):
- Both stock firmwares use the same radio registers: EN_AA=1, EN_RXADDR=1, SETUP_RETR=0x1A, RF_SETUP=0x0F, CONFIG 0x0E (TX) / 0x0F (RX), RX_PW_P0=32. SETUP_AW stays at its default (5 bytes).
- **P906 pairing mode:** in the P906 menu, open page 2/6 "TX and RX Addr" and start "Match RX Addr..". It then listens on channel 78 at FF FF FF FF FF.
- The M01 sends type 5 `'M'` and the P906 replies with its 4-byte ID (this unit: `0B 3D C1 C1`). The M01 then sends type 6 `addr[5] (register order) + channel`, and the P906 replies type 6 `"OK" 02`. Afterwards it answers on the new address immediately, with the address bytes in the same order as sent.
- Stock M01 v1.34 stores the ID at 0x0803C000 as one byte per u16 (`0b00 3d00 c100 c100`).
- Our firmware saves `{magic 'M0LC', addr[5], ch, id[4]}` at 0x08040000.
- Status poll = type 7 with `id[4], slot 0, blink 1`. The reply is 0x1C bytes: err, lock, temp BCD (35.5 °C), state, input V BCD (20.056 V), input I BCD (0.110 A), 4 raw ADC V/I pairs, set V BCD, set I BCD.

## Plan for replacement firmware
1. ~~Back up the full flash over SWD~~ (done, see above). Rig: USB-A socket, 4-pin row: GND→GND, D+→SWDIO, D−→SWCLK, VBUS→5V.
2. Toolchain: arm-none-eabi-gcc + CMake + OpenOCD/CubeProgrammer. Link at 0x08008000, VTOR = 0x08008000, and keep SWD enabled.
3. Bring-up: LCD + backlight → keys/encoders → Si24R1 status polling (read-only) → control → USB.
4. New UI (LVGL v9 or custom) designed around two encoders + 5 keys.

**Hard requirement:** keep the stock pairing workflow: Auto Match with the P906, and addresses/channels in `RX_ADDR.TXT` (`AddrN=`/`FreqN`). Existing pairings must keep working without re-pairing. The firmware must also **re-pair an unpaired P906 on its own** ("Pair P906" in the UI plus a key held at boot), using the same Auto Match exchange (type 5 call-for-ID on ch 78 → type 6 dispatch address/channel), and save the result to `RX_ADDR.TXT`. This needs the W25Q64 FAT drive read (FatFs) and, later, USB MSC so the PC can still edit the file.

Clock: stock firmware and bootloader both use HSE × 9 (`RCC_CFGR` 0x001D0000): 8 MHz crystal, 72 MHz.
