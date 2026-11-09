---
name: p906-firmware-findings
description: "Reverse-engineering findings on MDP-P906 v3.01 firmware and bootloader (DFU bug, USB-PD policy, current limit) and the 28 V patch decision"
metadata:
  node_type: memory
  type: project
  originSessionId: f2752340-1e00-4e4e-8977-598ba3c7fb12
  modified: 2026-11-09T12:58:02.799Z
---

Findings from analysing the P906 bootloader dump and v3.01 firmware (2026-11):
- Bootloader virtual DFU disk (FAT12, data region at byte 0x5000, first free cluster at 0x6A00): for .bin (after .ADR) it guesses the file start from fixed thresholds (0x6C00 / 0x8C00) instead of the directory entry's start cluster, and never validates the image before renaming to .RDY. Safe procedure: fresh DFU session, copy only the .hex (CRLF line endings), nothing else on the drive.
- USB-PD (v3.01): parses fixed/PPS/EPR(AVS) offers, requests 5 V then highest ≤ 20 V; enters EPR mode but still requests 20 V (0x0801ED92 `movw r0,#0x4e20`). Input current limiter = negotiated current; >3 A needs a 5 A e-marked cable. User confirmed MAX 5.00 A with a strong charger + good cable.
- "Current Limit Mode" (Normal/Unlimit) only toggles GPIO PE5; "Input Current Limit" sets a DAC-driven hardware input limiter (setting at RAM 0x200053F4, mA).
- A 28 V EPR patch was built (patched/MDP_P906_v3.01_pd28v.hex, 3 bytes changed) but NOT flashed: the USB-C input switch is an AON7409 (−30 V VDS, ±25 V VGS), too little margin at 28 V. K34 diode = 40 V Schottky, fine.

**Why:** avoids redoing the reverse engineering and re-proposing the unsafe 28 V patch.
**How to apply:** treat 20 V × 5 A (100 W) as the USB-C ceiling for this board; for more power use the 30 V barrel jack. Related: [[p906-recovery]].
