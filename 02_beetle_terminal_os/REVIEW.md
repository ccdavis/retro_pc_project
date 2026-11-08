# Review of `02_beetle_terminal_os` (2026-11-07)

| # | Finding | Fix / status |
| :-- | :-- | :-- |
| 1 | Plan: Beetle "512 KB SRAM, 2 MB flash" for ESP32 / C6 / S3 variants | C6 512 KB, C3 400 KB, both 4 MB flash; the classic Beetle ESP32 is 35 x 34 mm; there is no Beetle ESP32-S3. Memory budget redone: 192 KB of Z80 RAM does not fit beside Wi-Fi, BLE and SSH. |
| 2 | `beetle_cpm22_ssh.ino` presented as "192 KB CP/M 2.2 (RunCPM)" able to run WordStar etc. | It interprets eleven opcodes for four utilities. Rewritten as a truthfully described CP/M-style host shell; unknown opcodes print a pointer to `runcpm_patch/`. |
| 3 | `make_cpm_tools.py`: usage message address hard-coded to 0x0116 for a 30-byte prologue | SSH.COM and TELNET.COM printed `0E DC CD 05 00 C3 00 00` before "Usage:" (confirmed under z80pack cpmsim). Address now computed with an assert; regenerated `disk_A/*.COM` print cleanly under cpmsim. |
| 4 | `video_composite_spi.h`: `SPI.writeBytes()` (mutex) inside a 64 us timer ISR, 83 % of the core blocked in the ISR, glyphs from an arithmetic hash, `extern` font never defined | Cannot work on a single-core C3/C6 with BLE and Wi-Fi. Replaced by a documented stub that keeps the VT100 model, uses the public-domain font8x8 and refuses to build without `COMPOSITE_EXPERIMENTAL`. |
| 5 | `rp2350_hdmi_vt100_card.c`: parsed escape codes into a grid and output nothing ("link with the pico-examples encoder") | Implemented: `common/hstx_dvi.c` (HSTX DVI 640x480, two DMA channels, RGB565 scanline callback, 252 MHz / clk_hstx 126 MHz for the exact 252 Mbit/s TMDS rate) and an 80x30 text renderer with 16 colours, SGR, DEC cursor on/off, interrupt-driven UART ring. Builds with the Pico SDK: 12,028 bytes text, 14,740 bss, UF2 produced. Not run on hardware. |
| 6 | Both sketches: NimBLE 1.x API; dangling scan-result pointer; GPIO 16/17 fixed pins | NimBLE 2.x, address copy, variant `TX`/`RX`. |
| 7 | `WINDOW_DEFAULT` / `BUFFER_SIZE_MAX` defined in the sketch "to tune LibSSH" | They have no effect on a precompiled library; removed. |
| 8 | `ssh_options_set(SSH_OPTIONS_PORT, &int)` | libssh expects `unsigned int*`; fixed. |
| 9 | No key auto-repeat from the BLE keyboard | Added (400 ms delay, 30 Hz) in Option A. |
| 10 | SSH host key never verified | Still true; now stated in the sketch and plan. |
| 11 | `ed` with an empty file name wrote to an invalid handle silently | Checked. |
| 12 | `PIP` printed "Copied" only on success and nothing on a full RAM disk | Reports both. |
| 13 | CCP upper-cases the command tail; `SSH` forced lower case, `WIFI` did not say so | Documented in the sketch and plan. |
| 14 | RunCPM integration was only a sentence | `runcpm_patch/runcpm_cpm_h.patch` (applies to RunCPM commit 12fc57a) + `runcpm_host_bdos.h` + README. **Run-time verified** on the Linux build: the internal CCP runs the generated utilities and BDOS 220-223 (and 224 for project 5's SPEAK.COM) arrive at the host hook with the command tail. |

| 15 | Compiling found: the per-chip `CPM_EXPANDED_BANKS` default sat before `Arduino.h`, where `CONFIG_IDF_TARGET_ESP32C3` is not yet defined, so both chips got 4 banks (181 KB of globals, 36 KB of heap left after the radios: SSH would have failed) | Moved after the includes; C6 2 banks, C3 1 bank: 149 / 127 KB of globals. |
| 16 | Default 1.25 MB app partition too small for the 1.4-1.6 MB images | Build with the Huge APP scheme (`tools/compile_sketch.sh` adds it; Tools > Partition Scheme in the IDE). |

Verified here: cpmsim runs of SSH.COM, TELNET.COM, WIFI.COM; Pico SDK build of the HDMI card;
`git apply --check` of the RunCPM patch; **both sketches compile for the Beetle ESP32-C6 and
ESP32-C3** (arduino-esp32 3.3.12, NimBLE-Arduino 2.5.2, LibSSH-ESP32 5.10.0) with the sizes in
PLAN.md.  Also verified: the RunCPM patch at run time (Linux build, `CCP=INTERNAL`).  Not verified:
running on hardware, HSTX output on a monitor, the Beetle-side RunCPM port (LittleFS
abstraction and the host handlers), which is still to be written.
