---
name: p906-firmware-findings
description: "Reverse-engineering findings on MDP-P906 v3.01 firmware and bootloader (DFU bug, USB-PD policy, current limit) and the 28 V patch decision"
metadata:
  node_type: memory
  type: project
  originSessionId: f2752340-1e00-4e4e-8977-598ba3c7fb12
  modified: 2026-11-09T15:05:48.947Z
---

Findings from analysing the P906 bootloader dump and v3.01 firmware (2026-11):
- Bootloader virtual DFU disk (FAT12, data region at byte 0x5000, first free cluster at 0x6A00): for .bin (after .ADR) it guesses the file start from fixed thresholds (0x6C00 / 0x8C00) instead of the directory entry's start cluster, and never validates the image before renaming to .RDY. Safe procedure: fresh DFU session, copy only the .hex (CRLF line endings), nothing else on the drive.
- USB-PD (v3.01): parses fixed/PPS/EPR(AVS) offers, requests 5 V then highest ≤ 20 V; enters EPR mode but still requests 20 V (0x0801ED92 `movw r0,#0x4e20`). Input current limiter = negotiated current; >3 A needs a 5 A e-marked cable. User confirmed MAX 5.00 A with a strong charger + good cable.
- "Current Limit Mode" (Normal/Unlimit) only toggles GPIO PE5; "Input Current Limit" sets a DAC-driven hardware input limiter (setting at RAM 0x200053F4, mA).
- A 28 V EPR patch was built (patched/MDP_P906_v3.01_pd28v.hex, 3 bytes changed) but NOT flashed: the USB-C input switch is an AON7409 (−30 V VDS, ±25 V VGS), too little margin at 28 V. K34 diode = 40 V Schottky, fine.

- DFU not enumerating on the user's AMD/USB4 laptop: bootloader and app have identical USB descriptors and both use USB interrupts; clock is HSE×9 in both. Difference: the bootloader never configures the FUSB302 (app bit-bangs I2C to it). At reset the FUSB302 presents Rd on both CC pins = USB-C "debug accessory", which strict USB-C hosts mishandle. That hypothesis was tested on 2026-11-09: DFU still failed on what was probably a USB-A port, so it's likely wrong. Untested remaining suspect: insufficient 3.3 V rail in DFU when powered from the laptop port. The user chose to just use another PC for DFU updates. Never suggest barrel jack + USB-C together (the manual forbids powering both).

- DFU .ERR on the user's other PC (even stock .hex; file hash/size verified, no extra files on drive): the bootloader starts parsing at disk 0x7A00 when data is written before the dir entry, but the file starts at 0x6A00, so parsing begins mid-line. tools/dfuhex.py makes sector-aligned files with a 4 KB ignorable lead-in (patched/*_dfu.hex). blsim passes. CONFIRMED on hardware 2026-11-09: stock_dfu.hex flashed via DFU (.rdy, boots) on the PC where the plain .hex gave .ERR. Always use *_dfu.hex for DFU. pdfix_dfu.hex then flashed: problem chargers now trigger, but negotiation is slow (unquantified). Stock firmware was slow too. Added to pdfix: AUTO_CRC from init (0x08021A42/0x08021A80), accept the first caps (0x0801EC2E beq→nop), first request 20 V (0x0801ECD0). Flashed and working. Holding RUN to power on from fully off looks deliberate (standby routine 0x080126CC waits for a key held 2.5 s; the bootloader also waits 300 ms). The user accepted it, so don't patch it. Boot-time PD init order (after UI init at 0x08028B34) not changed.
- Charger-compat patch built 2026-11-09: patched/MDP_P906_v3.01_pdfix.hex from tools/pdfix.py (CC fix, AUTO_RETRY, protocol replies, request sent from the ISR; cave at 0x0802D100). NOT yet flashed or tested on hardware.
- Charger compatibility analysis: FUSB302 driver at 0x0802196C bit-bangs I2C. (1) Attach detect at 0x080219F8 `cmp r0,#2` requires BC_LVL≥2, so Rp ≥1.5 A; Default-USB Rp sources are never attached ("fusb3022 CC1 or CC2 error"), 3 tries at boot only. 1-byte fix: #2→#1. (2) Rx dispatcher 0x0801EBC0 only handles Accept, PS_RDY, Source_Caps, EPR_Mode, EPR_Source_Caps. Soft_Reset, Get_Sink_Cap, VDMs, Reject and Wait are ignored, so strict sources go to Hard Reset loops. Fixing that needs a code cave after 0x0802D0E8. (3) AUTO_CRC is off until the first Source_Caps arrives (Switches1=0x21/0x22, SPECREV=2.0), so the first caps are never acknowledged. (4) The Request is sent from the main loop (4 ms PD tick), so it may miss tSenderResponse. (5) PPS is preferred over fixed 20 V when the charger offers PPS up to ≥20 V. Control-message builder 0x0801E258 leaves the spec-revision bits at 0.

- 2026-11-09 added to pdfix (not yet flashed): prefer fixed PDO over PPS/AVS (0x0801E384/0x0801E3DA cmp best,target + beq), PPS keepalive only when the contract is PPS (0x0801ED02, 0x0801ED3C rewritten), version "3.01PD" (0x08019D7C).
- Fan (0x080179F8): NTC on ADC ch2 via LUT 0x0802AB54 (−1…79 °C in 0.5 °C steps), TIM4 CCR2 25 kHz PWM. 0 % below 40 °C, (T−39)×6 % above, 100 % from 55.7 °C, no hysteresis. OTP at ≥70 °C or ≤0 °C. Boot tach check on PE2 (0x080182F0). M01 work happens in a separate session.

**Why:** avoids redoing the reverse engineering and re-proposing the unsafe 28 V patch.
**How to apply:** treat 20 V × 5 A (100 W) as the USB-C ceiling for this board; for more power use the 30 V barrel jack. Related: [[p906-recovery]].
