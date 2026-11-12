# AirTag-Sized Retro Computer Project

This repository contains hardware designs, firmware, and emulators for building miniature wireless-keyboard retro computers and game consoles around the Waveshare RP2350-Zero (23.5 × 18.0 mm) and DFRobot Beetle ESP32-C6 / ESP32-C3 (25.0 × 20.5 mm), sized to fit an Apple AirTag or similar coin-sized enclosure.

For detailed hardware specifications, wiring tables, memory budgets, build instructions, and current status, see [OVERVIEW.md](OVERVIEW.md). Each project directory also has its own `PLAN.md`.

## Directories

### 1. [`01_ti994a_emulator/`](01_ti994a_emulator/) — TI-99/4A Console Emulator
Pairs a Beetle ESP32-C6/C3 with a Waveshare RP2350-Zero to run the `pico-994A` Texas Instruments TI-99/4A emulator with 640×480 HDMI output. The Beetle acts as a Bluetooth Low Energy (BLE) keyboard host and serves a Wi-Fi web pad for the TI-99/4A `FCTN` keys, forwarding input to the RP2350 over UART1 or PS/2.

### 2. [`02_beetle_terminal_os/`](02_beetle_terminal_os/) — VT100 Terminal, Micro-Shell & CP/M Environment
Turns the Beetle ESP32-C6/C3 into a standalone VT100 terminal and SSH-2 client (`LibSSH-ESP32`), using an RP2350-Zero as a 921600-baud UART-to-HDMI 80×30 color ANSI display card. Includes two firmware options: a FreeRTOS shell with local LittleFS storage, `ssh`, `telnet`, and `ed`, and a CP/M-style command shell with Z80 utilities (`SSH.COM`, `TELNET.COM`, `WIFI.COM`, `BANK.COM`) plus a patch for running full Z80 CP/M 2.2 via RunCPM.

### 3. [`03_rp2350_standalone_emulators/`](03_rp2350_standalone_emulators/) — RP2350 Multi-System Home Computer Emulators
Covers running 8-bit and 16-bit home computers (Apple //e, Apple //c, IBM PCjr, IBM PC XT, Commodore 64, and TI-99/4A) on the RP2350-Zero, either standalone or with the Beetle as a wireless keyboard and disk/modem companion. Includes a Core 1 scanline video renderer driving the RP2350's hardware HSTX DVI output and a script to fetch existing open-source RP2350 emulators.

### 4. [`04_game_consoles_and_amiga/`](04_game_consoles_and_amiga/) — Game Consoles & 68000 Systems
Covers 8-bit and 16-bit consoles (ColecoVision, Atari 5200, Atari 7800, NES, Master System, Genesis, SNES) and Motorola 68000 machines. Includes Core 1 scanline renderers for the Atari 7800 MARIA, Atari 5200 GTIA, and Macintosh 512×342 1-bit display on the RP2350-Zero, along with an Amiga 500/1200 hardware design based on `NanoMig` and the Sipeed Tang Primer 25K FPGA module.

### 5. [`05_accessible_speech_retro_pc/`](05_accessible_speech_retro_pc/) — Accessible "Talking" Retro Computer
Uses Core 1 of the RP2350-Zero as a dedicated speech synthesizer (DECtalk 4.x and ESPK/Klatt) and out-of-band screen reader while Core 0 runs the vintage system. Exposes a virtual RC Systems DoubleTalk LT serial synthesizer and reads emulated video RAM directly from shared SRAM so CP/M 2.2, DOS (`PROVOX7.EXE`), and Apple //e software can speak without using emulated memory or CPU cycles.

## Shared Code and Tooling

- [`common/`](common/) — Shared RP2350 HSTX 640×480 DVI scanline driver ([`hstx_dvi.c`](common/hstx_dvi.c)) and 8×8 bitmap font ([`font8x8_basic.h`](common/font8x8_basic.h)).
- [`tools/`](tools/) — Build scripts ([`compile_sketch.sh`](tools/compile_sketch.sh), [`install_arduino_cli.sh`](tools/install_arduino_cli.sh), [`install_rp2350_tools.sh`](tools/install_rp2350_tools.sh)) and local toolchains (ARM GCC, Pico SDK, Arduino CLI, QEMU, RunCPM).

## License

Copyright (C) 2026 Colin Davis.

This project is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE](LICENSE).

Third-party code keeps its original copyright and license:

- [`05_accessible_speech_retro_pc/third_party/espk/`](05_accessible_speech_retro_pc/third_party/espk/) — a subset of [eSpeak NG](https://github.com/espeak-ng/espeak-ng) (GPL-3.0-or-later, © Jonathan Duddington, Reece H. Dunn and contributors), including the derived `ESPK.DAT` voice data.
- [`05_accessible_speech_retro_pc/third_party/klatt/`](05_accessible_speech_retro_pc/third_party/klatt/) — a fixed-point port of `parwave.c` from klatt 3.04 (GPL-3.0-or-later, © Jon Iles, Nick Ing-Simmons, Reece H. Dunn).
- **DECtalk is not included.** [`get_dectalk.sh`](05_accessible_speech_retro_pc/third_party/dectalk/get_dectalk.sh) downloads it at build time from [github.com/dectalk/dectalk](https://github.com/dectalk/dectalk), whose source is under a proprietary Fonix license. Only small glue code and a patch are tracked here. Firmware built with DECtalk can't be redistributed under the GPL.
- Emulators and SDKs fetched by the build scripts (pico-994A, RunCPM, pico-sdk, and others) keep their own licenses. The `.patch` files in this repo modify those projects.
