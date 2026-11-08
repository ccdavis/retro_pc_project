# Review of `03_rp2350_standalone_emulators` (2026-11-07)

| # | Finding | Fix / status |
| :-- | :-- | :-- |
| 1 | `standalone_emulator_main.c` filled a ping-pong buffer that nothing scanned out; no HSTX, no DMA, no CPU | Core 1 now drives `common/hstx_dvi.c` (real HSTX DVI output, line callback) and renders the selected machine's scanline doubled and centred; Core 0 is explicitly a stub (Beetle UART frames, test pattern). Builds for all four machines with the Pico SDK (about 10.7 KB text + 267 KB bss for the 256 KB emulated RAM), UF2 produced. |
| 2 | One 16-colour palette (the C64's) used for CGA/PCjr and the TMS9918A | Three palettes: C64 (Pepto), CGA/PCjr, TMS9918A. |
| 3 | TMS9918A renderer drew sprites only: no name/pattern/colour tables, no border, 8x8 sprites only | Graphics I and II backdrop from R0-R7, border colour, 16x16 and magnified sprites, fifth-sprite index, coincidence. Text and Multicolor modes still missing (stated). |
| 4 | C64 renderer hard-coded screen at $0400 and ignored $D018 and the VIC bank; plan claimed multicolour and X/Y expansion | Screen and character bases from $D018 and the bank, ROM characters where the VIC sees them; the plan now lists what is not implemented. |
| 5 | Apple II hi-res coloured every lit pixel, so adjacent pixels never rendered white | Adjacent lit pixels render white; colours by parity and high bit. |
| 6 | Plan: "384 KB PC XT in stock SRAM" with pico-286 | pico-286's own README: 200 KB without PSRAM, PSRAM recommended, Murmulator pinout. Corrected. |
| 7 | Plan: "520 KB across 10 banks" | 8 x 64 KB + 2 x 4 KB. |
| 8 | Plan HDMI pin order and Type A connector numbers | Pico DVI Sock / PicoPlus order, Type D numbers; `HSTX_DVI_PINOUT_M2` for frank-apple's order. |
| 9 | Plan: UART link on GPIO 0/1, PSRAM CS on GPIO 8, PS/2 on 6/7 "out of the box" | UART1 on 8/9, PSRAM CS on 20, PS/2 needs the emulator's pin edit. |
| 10 | Build script | URLs all resolve (checked); unchanged apart from nothing; it only clones. |

Not done: any CPU core; sound; running on hardware.
