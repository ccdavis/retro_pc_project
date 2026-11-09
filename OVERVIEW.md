# Technical Overview

This document summarizes the hardware specifications, shared drivers, wiring, build tooling, and implementation status across the five subprojects in this repository.

---

## 1. Hardware Platform and Mechanical Constraints

An Apple AirTag measures 31.9 mm in outer diameter and 8.0 mm thick, with an internal cavity roughly 29 mm across and 6.5 mm deep. Fitting a rectangular PCB inside requires either a board diagonal under ~29 mm, corner trimming, or a slightly larger 3D-printed shell.

| Board | Processor & Memory | Dimensions | Diagonal | Fit Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Waveshare RP2350-Zero** | RP2350A (dual Cortex-M33 @ 150 MHz), 520 KB SRAM (8×64 KB + 2×4 KB), 4 MB flash, HSTX on GPIO 12–19 | 23.5 × 18.0 mm | 29.6 mm | Tight; the 3.2 mm tall USB-C socket needs a rim cutout |
| **DFRobot Beetle ESP32-C6** (DFR1117) | Single-core RISC-V @ 160 MHz, 512 KB SRAM (320 KB usable DRAM), 4 MB flash, Wi-Fi 6 + BLE 5 | 25.0 × 20.5 mm | 32.3 mm | Diagonal exceeds 31.9 mm; corners must be trimmed ~2 mm or housed in a wider shell |
| **DFRobot Beetle ESP32-C3** (DFR0868) | Single-core RISC-V @ 160 MHz, 400 KB SRAM (320 KB usable DRAM), 4 MB flash, Wi-Fi 4 + BLE 5 | 25.0 × 20.5 mm | 32.3 mm | Same footprint as the C6 |
| **DFRobot Beetle ESP32** (DFR0575) | Dual-core Xtensa LX6, 520 KB SRAM | 35.0 × 34.0 mm | 48.8 mm | Does not fit the AirTag form factor |
| **Sipeed Tang Primer 25K SoM** | Gowin GW5A-LV25 FPGA (23,040 LUTs) | 23.0 × 18.0 mm | 29.2 mm | Requires a small carrier PCB with SDRAM and Micro-HDMI |

---

## 2. Standard Wiring and Pinout

Across the dual-board designs, the Beetle ESP32-C6/C3 handles wireless input and networking while the RP2350-Zero handles emulation and HDMI scan-out.

### Inter-Board Link (Beetle ↔ RP2350-Zero)
- **Power:** Beetle `VIN` (5 V) ↔ RP2350-Zero `5V`, `GND` ↔ `GND`.
- **UART HID Link (Project 1, 3, 5):** Beetle `TX` (C6: GPIO 16, C3: GPIO 21) → RP2350-Zero `GPIO 9` (`UART1 RX`) at 115200 baud; Beetle `RX` (C6: GPIO 17, C3: GPIO 20) ← RP2350-Zero `GPIO 8` (`UART1 TX`). `UART0` (`GPIO 0/1`) stays free for console output.
- **UART Terminal Link (Project 2):** Beetle `TX` → RP2350-Zero `GPIO 1` (`UART0 RX`) at 921600 baud.
- **PS/2 Link:** Beetle `GPIO 4` (CLK) → RP2350-Zero `GPIO 6`; Beetle `GPIO 5` (DATA) → RP2350-Zero `GPIO 7`.

### RP2350-Zero Peripherals
- **MicroSD (SPI0):** `SCK` on GPIO 2, `MOSI` on GPIO 3, `MISO` on GPIO 4, `CS` on GPIO 5 (matches `pico-994A` `HW_CONFIG` 2 and 6).
- **Optional 8 MB QSPI PSRAM (`APS6404L-3SQR`):** `CS#` on GPIO 20.
- **PWM Headphone Audio (Project 5):** Left on GPIO 20, Right on GPIO 21 (through a 220 Ω + 100 nF RC filter).
- **Micro-HDMI Type D (HSTX on GPIO 12–19, 270 Ω series resistors, Pico DVI Sock lane order):**
  - `GPIO 12 / 13` → TMDS Data 0 `+ / -` (Type D pins 9 / 11)
  - `GPIO 14 / 15` → TMDS Clock `+ / -` (Type D pins 12 / 14)
  - `GPIO 16 / 17` → TMDS Data 2 `+ / -` (Type D pins 3 / 5) — note that the RP2350-Zero onboard WS2812 LED sits on GPIO 16
  - `GPIO 18 / 19` → TMDS Data 1 `+ / -` (Type D pins 6 / 8)

---

## 3. Shared Code (`common/`) and Tooling (`tools/`)

- **[`common/hstx_dvi.c`](common/hstx_dvi.c) & [`common/hstx_dvi.h`](common/hstx_dvi.h):** Drives 640×480 @ 60 Hz RGB565 DVI output using the RP2350 hardware HSTX peripheral. Clocks the system at 252 MHz and `clk_hstx` at 126 MHz (DDR for a 252 Mbps TMDS bit rate) and feeds the HSTX FIFO via two ping-pong DMA channels, invoking a caller-supplied scanline callback. Supports both the Pico DVI Sock pin order (default) and the `frank-apple` M2 pin order (`HSTX_DVI_PINOUT_M2`).
- **[`common/font8x8_basic.h`](common/font8x8_basic.h):** Public-domain 8×8 monochrome ASCII bitmap font.
- **[`tools/compile_sketch.sh`](tools/compile_sketch.sh):** Compiles Arduino sketches for `c6` (`dfrobot_beetle_esp32c6`) or `c3` (`dfrobot_beetle_esp32c3`) with the `huge_app` (3 MB No OTA / 1 MB SPIFFS) partition scheme required by the BLE + Wi-Fi + SSH binaries.
- **[`tools/install_arduino_cli.sh`](tools/install_arduino_cli.sh):** Installs `arduino-cli`, the ESP32 core (3.3.12), `NimBLE-Arduino` (2.5.2), and `LibSSH-ESP32` (5.10.0) into `tools/arduino/`.
- **[`tools/install_rp2350_tools.sh`](tools/install_rp2350_tools.sh):** Installs `pico-sdk` (2.1.1), `arm-gnu-toolchain` (13.3.rel1), `qemu` (`qemu-system-arm`), and `RunCPM` (6.9 with the BDOS 220–224 patch applied) into `tools/`.

---

## 4. Subproject Details

### 1. [`01_ti994a_emulator/`](01_ti994a_emulator/) — TI-99/4A Console Emulator
Runs the [`PicoPlus-devel/pico-994A`](https://github.com/PicoPlus-devel/pico-994A) emulator (TMS9900 CPU, TMS9918A VDP, TMS9919 sound, TMS5200 speech, 32 KB expansion RAM) on the RP2350-Zero with a Beetle wireless input bridge.
- **[`beetle_ble_bridge.ino`](01_ti994a_emulator/beetle_firmware/beetle_ble_bridge.ino):** NimBLE 2.x BLE keyboard host for the Beetle ESP32-C6/C3. Streams 13-byte HID frames (`AA 55 01 mod res k0..k5 joy xor`) over `UART1`, bit-bangs PS/2 Set 2 scancodes with host clock-inhibit support on GPIO 4/5, and serves a Wi-Fi web keypad at `http://airtag-ti99.local` for the TI-99/4A `FCTN` combinations.
- **[`uart_hid_bridge.cpp`](01_ti994a_emulator/rp2350_pico994a/uart_hid_bridge.cpp) & [`pico994a_uart_hid.patch`](01_ti994a_emulator/rp2350_pico994a/pico994a_uart_hid.patch):** Interrupt-driven `UART1` receiver that populates `pico-994A`'s `io::getCurrentKeyboardState()` and `io::getCurrentGamePadState(0)` structures.
- **[`build_pico994a.sh`](01_ti994a_emulator/rp2350_pico994a/build_pico994a.sh):** Clones `pico-994A`, applies the UART bridge patch, and builds `HW_CONFIG=2` with `USE_HSTX=1` for `waveshare_rp2350_zero`.
- **Status:** The Beetle sketch compiles for both C6 (1.43 MB flash / 51 KB static RAM) and C3 (1.27 MB / 46 KB). `pico-994A` builds cleanly to `pico994A.uf2` (447 KB flash / 251 KB BSS). Hardware testing is still to be done.

### 2. [`02_beetle_terminal_os/`](02_beetle_terminal_os/) — VT100 Terminal, Shell & CP/M Environment
Runs a local shell or CP/M-style environment with `LibSSH-ESP32` and Telnet on the Beetle ESP32-C6/C3.
- **Video Backends ([`video_backends/`](02_beetle_terminal_os/video_backends/)):**
  - [`rp2350_hdmi_vt100_card.c`](02_beetle_terminal_os/video_backends/rp2350_hdmi_vt100_card.c) & [`video_uart_rp2350_hdmi.h`](02_beetle_terminal_os/video_backends/video_uart_rp2350_hdmi.h): Primary video path. The RP2350-Zero receives ANSI/VT100 escape sequences over a 921600-baud UART into a 4 KB interrupt ring buffer and renders an 80×30 16-color text grid (`8×16` cells) over HSTX DVI (`common/hstx_dvi.c`). Builds with the Pico SDK to a 12 KB UF2 image.
  - [`video_vga_fabgl.h`](02_beetle_terminal_os/video_backends/video_vga_fabgl.h): FabGL VGA backend for the larger dual-core classic ESP32 only.
  - [`video_composite_spi.h`](02_beetle_terminal_os/video_backends/video_composite_spi.h): Non-working stub (`#error` unless `COMPOSITE_EXPERIMENTAL` is defined), retained only for its VT100 state machine reference.
- **OS Option A — FreeRTOS Shell ([`beetle_rtos_ssh_terminal.ino`](02_beetle_terminal_os/os_freertos_shell/beetle_rtos_ssh_terminal.ino)):** Provides `wifi scan/join/status`, `ssh <user>@<host> [port]`, `telnet`, `ls`, `cat`, `rm`, `ed`, and BLE keyboard input with key repeat. Uses 1.57 MB flash / 50 KB static RAM on C6 (1.43 MB / 45 KB on C3), leaving ~280 KB heap before radios. Note: the SSH client does not currently verify server host keys.
- **OS Option B — CP/M-Style Host Shell & RunCPM Patch ([`os_cpm22_runcpm/`](02_beetle_terminal_os/os_cpm22_runcpm/)):**
  - [`beetle_cpm22_ssh.ino`](02_beetle_terminal_os/os_cpm22_runcpm/beetle_cpm22_ssh.ino) provides an `A>` prompt (`DIR`, `TYPE`, `ERA`, `PIP`, `CLS`), a LittleFS `A:` drive, an expanded-memory RAM disk (`B:`/`M:`, 2×16 KB banks on C6, 1×16 KB on C3), and a minimal opcode interpreter specifically for the four generated utilities (`SSH.COM`, `TELNET.COM`, `WIFI.COM`, `BANK.COM` built by [`make_cpm_tools.py`](02_beetle_terminal_os/os_cpm22_runcpm/make_cpm_tools.py)), which trap into host BDOS functions 220–223.
  - [`runcpm_patch/`](02_beetle_terminal_os/os_cpm22_runcpm/runcpm_patch/) provides `runcpm_cpm_h.patch` and `runcpm_host_bdos.h` to add BDOS 220–224 to [RunCPM](https://github.com/MockbaTheBorg/RunCPM) for full Z80 emulation.
- **Status:** Both sketches compile for C6 and C3; the RP2350 HDMI card builds to UF2; `.COM` utilities are verified under `z80pack` `cpmsim` and on the Linux build of RunCPM. Porting RunCPM's SD filesystem layer to LittleFS on the Beetle and testing on hardware remain open.

### 3. [`03_rp2350_standalone_emulators/`](03_rp2350_standalone_emulators/) — RP2350 Multi-System Emulators
Evaluates running home computer emulators on the RP2350-Zero using Core 0 for the vintage CPU and Core 1 for scanline video and audio.
- **Memory Fit:** Systems up to 256 KB of RAM (Apple //e, Apple //c, Commodore 64, TI-99/4A, and 128–256 KB IBM PCjr) fit within the RP2350's 520 KB internal SRAM when using a 2-scanline DMA buffer (2.5 KB). A full 640 KB IBM PC XT (`pico-286`) requires an external 8 MB QSPI PSRAM chip (without PSRAM, `pico-286` provides 200 KB of conventional RAM).
- **[`rp2350_hstx_beamracer.h`](03_rp2350_standalone_emulators/dual_core_engine/rp2350_hstx_beamracer.h) & [`standalone_emulator_main.c`](03_rp2350_standalone_emulators/dual_core_engine/standalone_emulator_main.c):** Core 1 scanline renderers driving `common/hstx_dvi.c` for four video architectures:
  - Apple II Hi-Res / Double Hi-Res (with white adjacent-pixel artifacting).
  - IBM PCjr / CGA 320×200 16-color mode.
  - Commodore 64 VIC-II 40-column text mode with `$D018`/bank addressing and 8 sprites with collision detection (multicolor and bitmap modes not yet implemented).
  - TI-99/4A TMS9918A Graphics I/II backdrop plus 32 sprites (8×8, 16×16, magnification, 4-per-line limit, 5th-sprite and coincidence flags; text and multicolor modes not yet implemented).
- **[`build_all_rp2350_emulators.sh`](03_rp2350_standalone_emulators/build_all_rp2350_emulators.sh):** Clones existing RP2350 emulators (`rh1tech/frank-apple`, `ajtaji/AppleIIcEmulatorPicoSeries`, `xrip/pico-286`, `rh1tech/frank-c64`, `PicoPlus-devel/pico-994A`).
- **Status:** `dual_core_engine` builds with the Pico SDK into UF2 images for all four target machines (~10.7 KB text, 267 KB BSS), displaying test patterns and reading Beetle UART HID frames; Core 0 CPU emulation and audio synthesis are not yet hooked into the skeleton.

### 4. [`04_game_consoles_and_amiga/`](04_game_consoles_and_amiga/) — Game Consoles & 68000 Systems
Covers console and 68000 architectures on the RP2350-Zero and Sipeed Tang Primer 25K FPGA.
- **[`console_scanline_engines.h`](04_game_consoles_and_amiga/console_scanline_engines.h):** Header library of Core 1 scanline renderers:
  - **Atari 7800 `MARIA`:** Parses per-zone display lists (4-byte direct and 5-byte indirect character modes with `CHARBASE` and 2-byte characters in 160A 2-bpp mode) into a 160-byte line buffer. Does not yet include 160B/320 modes, holey DMA, Kangaroo mode, or the top-level Display List List (DLL) walker.
  - **Atari 5200 `GTIA`:** Overlays 4 players and 4 missiles with `SIZEP`/`SIZEM` horizontal scaling and priority rules; the underlying ANTIC display-list playfield renderer is not yet written.
  - **ColecoVision:** 12-button keypad matrix encoder (`0–9`, `*`, `#`).
  - **Macintosh 128K/512K:** 512×342 1-bit monochrome scanline scaler centered in a 640×480 frame.
- **[`build_consoles_and_68k.sh`](04_game_consoles_and_amiga/build_consoles_and_68k.sh):** Clones `alijani1/Pico2Atari8`, `PicoPlus-devel/pico-smsplus`, `pico-infonesPlus`, `pico-genesisPlus`, `pico-snesPlus`, `evansm7/pico-mac`, and `MiSTle-Dev/NanoMig`.

### 5. [`05_accessible_speech_retro_pc/`](05_accessible_speech_retro_pc/) — Accessible "Talking" Retro Computer
Runs a software speech synthesizer and screen reader on Core 1 of the RP2350-Zero while Core 0 runs the emulated computer.
- **Speech Engines ([`speech_core1/`](05_accessible_speech_retro_pc/speech_core1/) & [`third_party/`](05_accessible_speech_retro_pc/third_party/)):**
  - **DECtalk 4.x ARM7 (`engine_dectalk.c`, `third_party/dectalk/`):** Primary synthesizer with the full 329 KB US dictionary (`-DCHEESY_DICT_COMPRESSION`, fetched and compiled via [`get_dectalk.sh`](05_accessible_speech_retro_pc/third_party/dectalk/get_dectalk.sh)). Uses 667 KB flash, 54 KB static SRAM, 2.3 KB stack, 0 heap, and 3.5% of a 150 MHz Cortex-M33 core per second of speech (12.4% Whisper `base.en` WER).
  - **ESPK (`engine_espk.c`, `espk_data_mem.c`, `third_party/espk/`, `third_party/klatt/`):** Vendored eSpeak-NG English front end + integer Klatt (`klatt_fx` Q12), GPLv3+, serving `ESPK.DAT` directly from XIP flash. Uses 514 KB flash, 79 KB BSS, 47.5 KB stack, 12 KB heap, and 1.8% of a 150 MHz core.
  - **Virtual DoubleTalk LT (`doubletalk_core1.c`):** Implements SPSC ring buffers between Core 0 and Core 1, an atomic `^X` flush protocol, the DoubleTalk command parser, a 16550 UART register view (`COM2`), an 8192-sample 11025 Hz PCM ring buffer, an engine-independent startup chime, automatic engine fallback (`DECtalk → ESPK → tone`), and a Core 1 watchdog.
- **Hypervisor Screen Reader ([`screen_reader/tdsr_hypervisor_reader.c`](05_accessible_speech_retro_pc/screen_reader/tdsr_hypervisor_reader.c)):** Inspects shared video RAM (`TermCell` 80×30 VT100 grid for CP/M, `$0400`/`Aux` 40/80-column text for Apple //e including flashing and MouseText, and `B8000` CGA text for DOS) with scroll compensation, key-echo suppression, highlight tracking, and `rust-tdsr` review keys (`Alt+U/I/O`, `Alt+J/K/L`, `Alt+M/,/.`, `Alt+C`, `Alt+S`, etc.).
- **CP/M Speech Utility ([`cpm_disk_A/speak.asm`](05_accessible_speech_retro_pc/cpm_disk_A/speak.asm)):** Assembles to `SPEAK.COM` (`A>SPEAK [/Sn] [/Pnn] [/Vn] [/On] [/X] text`), which streams DoubleTalk commands through host BDOS 224.
- **Verification & Build ([`build_accessible_retro.sh`](05_accessible_speech_retro_pc/build_accessible_retro.sh)):**
  - `host_test/`: Runs 105 automated checks against both real speech engines and emits WAV files verified with Whisper.
  - `arm_qemu/`: Runs the same code on QEMU `mps2-an505` (Cortex-M33) with instruction counting; outputs WAVs byte-identical to the host build.
  - `pico_firmware/`: Builds `talking_retro.uf2` (1,275 KB flash, 220 KB BSS including a 64 KB Core 1 stack, leaving ~300 KB SRAM for Core 0) with PWM headphone output on GPIO 20/21.
- **Open Items:** Flashing to a physical RP2350-Zero board, adding HDMI L-PCM data-island audio packets to `common/hstx_dvi.c`, connecting full Core 0 CPU emulators, and adding a 16-bit output path for ESPK.
