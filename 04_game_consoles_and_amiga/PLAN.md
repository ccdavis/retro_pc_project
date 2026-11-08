# AirTag Game Consoles (ColecoVision, Atari 5200, Atari 7800, 16-Bit) & Amiga / 68000 Plan (`04_game_consoles_and_amiga`)

**Review status (2026-11-07):** repository links verified (the PicoPlus-devel organisation is the
current home of the `pico-*Plus` emulators; `harbaum/NanoMig` redirects to `MiSTle-Dev/NanoMig`);
`console_scanline_engines.h` now has MARIA indirect (character) mode and GTIA missiles and
sizes, and states what it does not do.  Memory claims corrected below.  See `REVIEW.md`.

---

## 1. The Surprising Truth: 8-Bit Game Consoles Are *Easier* Than Home Computers!

You might expect dedicated game consoles like the **ColecoVision**, **Atari 5200**, and **Atari 7800** to be harder to emulate on the **RP2350-Zero (+ Beetle)** than computers like the Apple //e or PC XT. 

In reality, **8-bit game consoles are easier and use far less RAM** for two reasons:
1. **Tiny RAM Footprints:** 1980s consoles had almost no system RAM (**1 KB** on ColecoVision, **4 KB** on Atari 7800, **16 KB** on Atari 5200) because games executed directly out of small **8 KB–48 KB ROM cartridges**—which can execute straight out of the RP2350-Zero's fast XIP Flash or 520 KB internal SRAM without needing floppy drive emulation!
2. **Their Custom Graphics Chips Were Built Around Scanline Line-Buffers:**
   * Real 1980s console chips couldn't afford a full-screen framebuffer in silicon either—they computed graphics **one scanline at a time** as the CRT electron beam swept across the TV.
   * That maps 1-to-1 onto **`Core 1` of the RP2350-Zero** feeding a **2-scanline ping-pong DMA buffer** into the hardware **HSTX HDMI** peripheral!

---

## 2. Console-by-Console Breakdown on the RP2350-Zero (+ Beetle)

| System | CPU | Graphics & Sound Hardware | Console RAM | Fits in Stock 520 KB SRAM? | Existing RP2350 Projects |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **ColecoVision** *(1982)* | Z80A @ 3.58 MHz | **TMS9928A VDP** (32 sprites) + **SN76489A** 3-voice PSG | **1 KB RAM** + 16 KB VRAM *(8K–32K ROMs)* | **Yes (Effortless!)** | `Adafruit_ColecoJam` / `PicoPlus-devel`, `MCUME` |
| **Atari 5200** *(1982)* | 6502C @ 1.79 MHz | **ANTIC** (Display Lists) + **GTIA** (4 Players + 4 Missiles) + **POKEY** 4-ch audio | **16 KB RAM** *(16K–32K ROMs)* | **Yes (Effortless!)** | [`alijani1/Pico2Atari8`](https://github.com/alijani1/Pico2Atari8), [`rh1tech/frank`](https://github.com/rh1tech/frank) |
| **Atari 7800** *(1984)* | 6502C @ 1.79 MHz | **MARIA** (Display List Line-Buffer Engine — 30+ sprites/line!) + **TIA / POKEY** | **4 KB RAM** *(16K–48K ROMs)* | **Yes (Effortless!)** | `ProSystem` / `Atari7800` RP2350 Core |
| **NES / Famicom** *(1983/85)* | 2A03 (6502) @ 1.79 MHz | **2C02 PPU** (64 sprites, 8/line, scrolling) + **5-ch APU** | **2 KB RAM** + 2 KB VRAM | **Yes!** (`pico_shared/BoardConfigs.cmake`: `HW_CONFIG 6` "RP2350-Zero with custom PCB" is PIO DVI on GPIO 6-13 with SD on 2-5; for this project's HSTX wiring on 12-19 build `HW_CONFIG 2` with `USE_HSTX=1`, as project 1 does) | [`PicoPlus-devel/pico-infonesPlus`](https://github.com/PicoPlus-devel/pico-infonesPlus) |
| **Sega Master System / Game Gear** | Z80A @ 3.58 MHz | **YM2602 VDP** (TMS9918 evolution) + **SN76489** PSG | **8 KB RAM** + 16 KB VRAM | **Yes!** | [`PicoPlus-devel/pico-smsplus`](https://github.com/PicoPlus-devel/pico-smsplus) |
| **Sega Genesis / Mega Drive** | Motorola 68000 @ 7.67 MHz + Z80 | **YM7101 VDP** (Dual planes, 80 sprites) + **YM2612 FM** + SN76489 | **64 KB RAM** + 64 KB VRAM + 8 KB Z80 | **RAM fits in 520 KB SRAM** *(Add $1.50 8MB PSRAM for 1MB–4MB ROMs)* | [`PicoPlus-devel/pico-genesisPlus`](https://github.com/PicoPlus-devel/pico-genesisPlus) |
| **Super Nintendo (SNES)** | 65C816 @ 3.58 MHz | **PPU1/PPU2** (Mode 7, 128 sprites) + **SPC700 8-ch DSP** | **128 KB RAM** + 64 KB VRAM + 64 KB Audio | **Requires $1.50 8MB PSRAM** *(for large ROMs & tables)* | [`PicoPlus-devel/pico-snesPlus`](https://github.com/PicoPlus-devel/pico-snesPlus) |

---

## 3. How ColecoVision, Atari 5200, and Atari 7800 Work Under the Hood

### A. ColecoVision: You Already Have 100% of the Parts!
Look at the chips inside a 1982 ColecoVision:
* **CPU:** Z80A @ 3.58 MHz (same CPU as our CP/M 2.2 setup).
* **Video:** **Texas Instruments TMS9928A** (the exact same VDP chip as the **TI-99/4A** in Section 1!).
* **Sound:** **Texas Instruments SN76489A** (the exact same 3-voice sound chip as the **TI-99/4A** and **IBM PCjr**!).
* **The Controller Keypad Trick:** The ColecoVision controller had a joystick plus a **12-button telephone keypad (`0–9`, `*`, `#`)** used with plastic overlays for games like *WarGames*, *Mouse Trap*, and *Spy Hunter*. With the **Beetle ESP32** inside your AirTag case, those 12 keypad buttons map directly to `0–9, -, =` on your wireless Bluetooth keyboard **and** appear as an interactive touch keypad on your phone browser via the Beetle's Wi-Fi web page!

### B. Atari 5200 (and Atari 800XL): `ANTIC` + `GTIA` Scanline Coprocessing
* The Atari 5200 is an **Atari 400/800 computer** consolidated into a console with **16 KB of RAM**.
* Instead of a static screen layout, the **ANTIC** chip executes a miniature **Display List program** on every frame (mixing text modes, bitmap modes, fine scrolling, and Display List Interrupts `DLIs` mid-screen), while the **GTIA** chip overlays **4 Players (8-bit wide full-screen-height vertical strips) and 4 Missiles (2-bit strips)**.
* Because Players and Missiles are 1-byte horizontal shift registers rather than 2D sprite tables, the GTIA overlay in the header (4 players and 4 missiles with SIZEP/SIZEM widths, players over missiles, lower-numbered player on top) is cheap; the ANTIC playfield itself (display list, character and map modes, DLIs) is still to be written.
* **Analog Joystick + 15-Button Keypad:** Like the ColecoVision, the Atari 5200 used a 12-button keypad (`0–9, *, #`) plus `Start`, `Pause`, and `Reset`—all handled cleanly by the wireless keyboard or the Beetle's Wi-Fi controller page.

### C. Atari 7800: Why `MARIA` Is a Dream Match for the RP2350-Zero
* Unlike the Atari 2600/5200 or NES, the Atari 7800's custom **MARIA** graphics chip has **no tile background and no 8-sprite limit**. Every single object on screen (backgrounds, scores, and 30+ moving enemies in *Robotron: 2084* or *Joust*) is a sprite entry in a per-zone **Display List (DL)**.
* **How Real MARIA Silicon Worked in 1984:** MARIA contained **two internal 160-byte line RAM buffers (Line Buffer A and Line Buffer B)**. While MARIA's DMA engine walked the Display List to paint sprites into Line Buffer A, it shifted Line Buffer B out to the TV!
* **How the RP2350-Zero Emulates MARIA:** Core 1 in [`console_scanline_engines.h`](file:///home/ccd/retro_pc_project/04_game_consoles_and_amiga/console_scanline_engines.h) parses a zone's display list, 4-byte direct and 5-byte headers including **indirect (character) mode with CHARBASE and 2-byte characters**, in 160A (2 bpp) into a 160-byte line RAM, then scales it to the HDMI line.  Not yet: 160B, the 320 modes, holey DMA, Kangaroo mode and the DLL walk (the caller supplies the zone), so "cycle-exact" is not the right word for it.

---

## 4. What About the Commodore Amiga (and Other 68000 Systems)?

When we step up to 16/32-bit **Motorola 68000** machines, there is a fascinating split between what runs on the **RP2350-Zero microcontroller** versus what requires stepping up to the **Tang Primer 25K FPGA SoM**:

### 1. 68000 Systems That *Do* Run on the RP2350-Zero:
* **Apple Macintosh 128K / 512K (`evansm7/pico-mac` / `umac`):**
  * Even though the 1984 Macintosh runs a 7.83 MHz Motorola 68000, it has **no custom graphics blitter, no sprites, and no color bitplanes**—just a flat `512×342` 1-bit black-and-white bitmap (`21,888 bytes` of RAM!).
  * Because of that, **`pico-mac` (`umac`) runs a Macintosh 128K inside the microcontroller's SRAM** and boots System 1-3, *MacPaint* and *MacWrite*.  Its README documents `MEMSIZE` up to about 208 KB (192-208 KB is needed for MacPaint); the RP2350's extra SRAM should allow more, but "384K/512K in SRAM" from the first draft is not something the project claims.  pico-mac's video is PIO/VGA-style; an HSTX HDMI path would be new work (`render_scanline_macintosh_512x342` in the header is ready for it).
* **Atari ST (`MCUME` / *Castaway* on RP2350 + $1.50 8MB PSRAM):**
  * The Atari 520ST/1040ST uses an 8 MHz 68000 + a straightforward 32 KB 4-bitplane framebuffer (`320×200×16` or `640×200×4`) + YM2149 PSG audio. With the 8-pin **8 MB QSPI PSRAM** chip soldered onto the RP2350-Zero, lightweight Atari ST emulation runs in software.

### 2. Why the Commodore Amiga 500 Is Too Heavy for Software on RP2350—And How to Do It With an AirTag-Sized FPGA (`NanoMig`)!
* **Why software Amiga emulation struggles on an RP2350:**
  * Unlike a Mac or Atari ST, an **Amiga 500** has **512 KB Chip RAM + 512 KB Slow RAM (1 MB)** where the 68000 CPU is constantly interleaved cycle-by-cycle with three custom DMA chips (**Agnus, Denise, and Paula**):
    * **The Copper:** Reprograms palette and display registers mid-scanline.
    * **The Blitter:** Performs 4-channel (`A, B, C -> D`) boolean bitplane DMA copies, barrel shifts, and line/area fills across Chip RAM.
    * **Denise & Paula:** Decode up to 6 bitplanes (64 colors / 4,096-color HAM mode) + 8 sprites + 4-channel DMA PCM audio.
  * Because a software Amiga emulator must hold 1 MB+ of RAM in external 4-bit QSPI PSRAM, the QSPI serial bus bottleneck prevents a 150–300 MHz microcontroller from emulating the 68000 + Blitter + Copper + Bitplanes at full 50/60 Hz speed.

* **The AirTag Amiga Solution — `NanoMig` on the Sipeed Tang Primer 25K FPGA SoM ($22.9 \times 17.8\text{ mm}$):**
  * As we found in our initial hardware survey, the **Sipeed Tang Primer 25K Core Board (SoM)** is just **$22.9 \times 17.8\text{ mm}$** (smaller than the RP2350-Zero and Beetle!) and packs a **23,040-LUT Gowin GW5A-LV25 FPGA**.
  * The open-source **[`MiSTle-Dev/NanoMig`](https://github.com/MiSTle-Dev/NanoMig)** project (by Till Harbaum) ports the full **MiSTer Minimig AGA** core to the Tang 20K / Primer 25K FPGA—giving you hardware-accurate **Amiga 500, Amiga 1000, and Amiga 1200 (68020 + OCS/ECS/AGA + 2 MB Chip RAM + 4 Floppy Drives + 2 IDE Hard Drives + HDMI Video/Audio)**!
  * Even better: **`NanoMig` is specifically architected to use a tiny companion microcontroller (like an RP2040/RP2350-Zero or ESP32) wired over SPI** to manage the On-Screen Display (OSD), mount `.ADF` floppy / `.HDF` hard drive images, and bridge wireless USB/Bluetooth keyboards, mice, and gamepads!

```
               THE "ULTIMATE AIRTAG AMIGA" STACK (NanoMig FPGA + Beetle/RP2350)
             +-----------------------------------------------------------------+
             |  Top Board:    Beetle ESP32 or RP2350-Zero (23.5 x 18.0 mm)     |
             |                - BLE Wireless Keyboard, Mouse & Gamepad Host    |
             |                - SPI OSD & .ADF / .HDF Disk Image Controller    |
             |                - Wi-Fi AT Modem & NTP / FTP Server              |
             +-----------------------------------------------------------------+
                                       | 4-Wire SPI + Power
                                       v
             +-----------------------------------------------------------------+
             |  Bottom Board: Sipeed Tang Primer 25K SoM (22.9 x 17.8 mm)      |
             |                + Custom 30mm Round Carrier w/ SDRAM & Micro-HDMI|
             |                - Runs 'NanoMig' (Amiga 500/1200 OCS/ECS/AGA)    |
             |                - Or 'MiSTeryNano' (Atari ST / STE)              |
             +-----------------------------------------------------------------+
```
