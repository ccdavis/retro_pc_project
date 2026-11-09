# Game Consoles & 68000 Systems (`04_game_consoles_and_amiga`)

**Target hardware:** Waveshare RP2350-Zero (+ optional Beetle ESP32-C6/C3 wireless bridge) for 8-bit/16-bit consoles and Macintosh 128K; Sipeed Tang Primer 25K FPGA SoM (23.0 × 18.0 mm) + RP2350/Beetle companion for cycle-accurate Amiga 500/1200 (`NanoMig`).

---

## 1. Why 8-Bit Consoles Fit Easily on the RP2350-Zero

Compared to 8-bit home computers, 1980s game consoles use less memory and map naturally to a dual-core scanline architecture:
1. **Small RAM Footprints:** Systems like the ColecoVision (1 KB RAM), NES (2 KB RAM), Atari 7800 (4 KB RAM), and Atari 5200 (16 KB RAM) execute directly out of 8–48 KB ROM cartridges, which fit inside the RP2350-Zero's XIP flash or 520 KB internal SRAM without needing floppy drive emulation.
2. **Line-Buffer Video Hardware:** Console graphics chips (such as the TMS9928A, 2C02 PPU, ANTIC/GTIA, and MARIA) were designed around per-scanline buffers rather than full-screen framebuffers. Core 1 of the RP2350-Zero renders each line into a 2-scanline ping-pong DMA buffer feeding the hardware HSTX HDMI peripheral.

---

## 2. Console Comparison on the RP2350-Zero

| System | CPU | Graphics & Sound Hardware | Console RAM | Fits in 520 KB SRAM? | Existing RP2350 Projects |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **ColecoVision** (1982) | Z80A @ 3.58 MHz | TMS9928A VDP (32 sprites) + SN76489A 3-voice PSG | 1 KB RAM + 16 KB VRAM (8–32 KB ROMs) | Yes | `Adafruit_ColecoJam` / `PicoPlus-devel`, `MCUME` |
| **Atari 5200** (1982) | 6502C @ 1.79 MHz | ANTIC (Display Lists) + GTIA (4 Players + 4 Missiles) + POKEY 4-ch audio | 16 KB RAM (16–32 KB ROMs) | Yes | [`alijani1/Pico2Atari8`](https://github.com/alijani1/Pico2Atari8), [`rh1tech/frank`](https://github.com/rh1tech/frank) |
| **Atari 7800** (1984) | 6502C @ 1.79 MHz | MARIA (Display List line-buffer engine) + TIA / POKEY | 4 KB RAM (16–48 KB ROMs) | Yes | `ProSystem` / `Atari7800` |
| **NES / Famicom** (1983/85) | 2A03 (6502) @ 1.79 MHz | 2C02 PPU (64 sprites, 8/line, scrolling) + 5-ch APU | 2 KB RAM + 2 KB VRAM | Yes (build `HW_CONFIG 2` with `USE_HSTX=1` for HSTX on GPIO 12–19; `HW_CONFIG 6` uses PIO DVI on GPIO 6–13) | [`PicoPlus-devel/pico-infonesPlus`](https://github.com/PicoPlus-devel/pico-infonesPlus) |
| **Sega Master System / Game Gear** | Z80A @ 3.58 MHz | YM2602 VDP (TMS9918 derivative) + SN76489 PSG | 8 KB RAM + 16 KB VRAM | Yes | [`PicoPlus-devel/pico-smsplus`](https://github.com/PicoPlus-devel/pico-smsplus) |
| **Sega Genesis / Mega Drive** | Motorola 68000 @ 7.67 MHz + Z80 | YM7101 VDP (dual planes, 80 sprites) + YM2612 FM + SN76489 | 64 KB RAM + 64 KB VRAM + 8 KB Z80 | RAM fits in 520 KB SRAM; add 8 MB PSRAM for 1–4 MB ROMs | [`PicoPlus-devel/pico-genesisPlus`](https://github.com/PicoPlus-devel/pico-genesisPlus) |
| **Super Nintendo (SNES)** | 65C816 @ 3.58 MHz | PPU1/PPU2 (Mode 7, 128 sprites) + SPC700 8-ch DSP | 128 KB RAM + 64 KB VRAM + 64 KB Audio | Requires 8 MB PSRAM for large ROMs and lookup tables | [`PicoPlus-devel/pico-snesPlus`](https://github.com/PicoPlus-devel/pico-snesPlus) |

---

## 3. Scanline Renderers in [`console_scanline_engines.h`](console_scanline_engines.h)

### A. ColecoVision
- Uses a Z80A @ 3.58 MHz, a **TI TMS9928A** VDP (the same video architecture as the TI-99/4A TMS9918A), and a **TI SN76489A** 3-voice PSG.
- The ColecoVision controller includes a 12-button telephone keypad (`0–9`, `*`, `#`). `encode_colecovision_keypad()` maps keys from the Bluetooth keyboard (or the Beetle's Wi-Fi web keypad) into the ColecoVision I/O port `0xFC` encoding.

### B. Atari 5200 (and Atari 400/800) — `ANTIC` + `GTIA`
- The Atari 5200 consolidates the Atari 400/800 architecture with 16 KB of RAM. The **ANTIC** chip executes a per-frame Display List program, while **GTIA** overlays 4 Players (8-bit horizontal shift registers) and 4 Missiles (2-bit shift registers).
- `render_atari5200_gtia_players()` in [`console_scanline_engines.h`](console_scanline_engines.h) renders the 4 players and 4 missiles with `SIZEP`/`SIZEM` horizontal width scaling (1×, 2×, 4×) and player-over-missile priority. The underlying ANTIC playfield renderer (display list walker, character/map modes, and DLIs) is still to be written.

### C. Atari 7800 — `MARIA` Line-Buffer Renderer
- Rather than a tile background with an 8-sprite limit, the Atari 7800 **MARIA** chip renders all screen objects from per-zone Display Lists into two internal 160-byte line RAM buffers (painting into one while shifting the other to the display).
- `render_scanline_atari7800_maria()` in [`console_scanline_engines.h`](console_scanline_engines.h) parses a zone's display list (both 4-byte direct headers and 5-byte headers, including indirect character mode with `CHARBASE` and 2-byte characters) in 160A (2 bpp) mode into a 160-byte line buffer and scales it to a 640-pixel HDMI line. Still to be implemented: 160B and 320 modes, holey DMA, Kangaroo mode, and the outer Display List List (DLL) walker.

---

## 4. Motorola 68000 Systems & Commodore Amiga

### A. 68000 Systems on the RP2350-Zero
- **Apple Macintosh 128K (`evansm7/pico-mac` / `umac`):**
  - The 1984 Macintosh runs a 7.83 MHz 68000 with no custom graphics blitter or sprites—only a flat `512×342` 1-bit monochrome framebuffer (`21,888 bytes` of RAM).
  - `pico-mac` (`umac`) runs inside internal SRAM (`MEMSIZE` up to ~208 KB, enough for System 1–3, *MacPaint*, and *MacWrite*). Upstream `pico-mac` outputs PIO/VGA video; `render_scanline_macintosh_512x342()` in [`console_scanline_engines.h`](console_scanline_engines.h) provides the 512×342-to-640×480 scanline scaler for an HSTX HDMI port.
- **Atari ST (`MCUME` / *Castaway* + 8 MB PSRAM):**
  - Uses an 8 MHz 68000, a 32 KB 4-bitplane framebuffer (`320×200×16` or `640×200×4`), and YM2149 PSG audio. Requires soldering an 8 MB QSPI PSRAM chip to the RP2350-Zero.

### B. Commodore Amiga 500/1200 via FPGA (`NanoMig` on Sipeed Tang Primer 25K)
- An Amiga 500 requires 512 KB Chip RAM + 512 KB Slow RAM (1 MB) with cycle-interleaved DMA across the 68000 CPU and the Agnus, Denise, and Paula custom chips (Copper, Blitter, 6-bitplane/HAM video, 8 sprites, 4-channel PCM audio). Holding 1 MB+ of Chip RAM over a 4-bit QSPI PSRAM bus bottlenecks software emulation on a microcontroller.
- **FPGA Architecture:** The **Sipeed Tang Primer 25K Core Board (SoM)** measures **23.0 × 18.0 mm** and carries a 23,040-LUT Gowin GW5A-LV25 FPGA.
- **[`MiSTle-Dev/NanoMig`](https://github.com/MiSTle-Dev/NanoMig)** ports the MiSTer Minimig AGA core to the Tang 20K / Primer 25K, implementing an Amiga 500/1000/1200 (68020, OCS/ECS/AGA, 2 MB Chip RAM, 4 floppy drives, 2 IDE hard drives, HDMI video/audio) and using a companion microcontroller (RP2040/RP2350-Zero or ESP32) over 4-wire SPI for the On-Screen Display (OSD), `.ADF`/`.HDF` disk mounting, and wireless keyboard/mouse/gamepad input.

```
               AMIGA STACK (NanoMig FPGA + Beetle/RP2350 Companion)
             +-----------------------------------------------------------------+
             |  Top Board:    Beetle ESP32-C6/C3 or RP2350-Zero                |
             |                - BLE Wireless Keyboard, Mouse & Gamepad Host    |
             |                - SPI OSD & .ADF / .HDF Disk Image Controller    |
             |                - Wi-Fi AT Modem & NTP / FTP Server              |
             +-----------------------------------------------------------------+
                                       | 4-Wire SPI + Power
                                       v
             +-----------------------------------------------------------------+
             |  Bottom Board: Sipeed Tang Primer 25K SoM (23.0 x 18.0 mm)      |
             |                + 30 mm Round Carrier w/ SDRAM & Micro-HDMI      |
             |                - Runs 'NanoMig' (Amiga 500/1200 OCS/ECS/AGA)    |
             |                - Or 'MiSTeryNano' (Atari ST / STE)              |
             +-----------------------------------------------------------------+
```
