# AirTag Retro Terminal & Minimal OS Plan (`02_beetle_terminal_os`)
**Target hardware:** DFRobot Beetle ESP32-C6 (512 KB SRAM, 4 MB flash) or Beetle ESP32-C3 (400 KB SRAM, 4 MB flash); both single-core RISC-V at 160 MHz, 25 x 20.5 mm.
**Goal:** a wireless-keyboard VT100 terminal in an AirTag-sized case with a local shell, flash files, Telnet and an SSH-2 client, and a CP/M flavour.

**Review status (2026-11-07):** the first draft described the Beetle as "512 KB SRAM, 2 MB flash"
for every variant (the C3 has 400 KB; all have 4 MB), listed a classic Beetle ESP32 that is
35 x 34 mm, used the NimBLE 1.x API, hard-coded GPIO 16/17 (absent on the C3), had a
composite video driver that cannot work, an HDMI card that produced no video, and a
"192 KB CP/M 2.2" that was a shell executing four tiny utilities.  What is in the directory
now and what it has been checked against is in `REVIEW.md`.  Both sketches compile for the
Beetle ESP32-C6 and ESP32-C3 with arduino-esp32 3.3.12, NimBLE-Arduino 2.5.2 and LibSSH-ESP32
5.10.0 (`tools/compile_sketch.sh`); they need the **"Huge APP (3 MB No OTA / 1 MB SPIFFS)"**
partition scheme because the images are 1.4-1.6 MB.  Nothing has run on hardware.

---

## 1. Memory budget

The Arduino core exposes **320 KB of DRAM** to the application on both the C6 (512 KB SRAM)
and the C3 (400 KB); the rest is instruction RAM, cache and ROM data.  Sizes below are from
the actual builds (arduino-esp32 3.3.12).

| | Flash image | Static RAM | Heap left before radios |
| :-- | --: | --: | --: |
| Option A shell, C6 / C3 | 1.57 / 1.43 MB | 50 / 45 KB | 277 / 283 KB |
| Option B CP/M-style shell, C6 (2 banks) / C3 (1 bank) | 1.58 / 1.43 MB | 149 / 127 KB | 179 / 200 KB |
| Project 1 BLE bridge, C6 / C3 | 1.43 / 1.27 MB | 51 / 46 KB | 276 / 282 KB |

Run-time needs on top: Wi-Fi + lwIP about 70 KB, NimBLE about 40 KB, an SSH-2 session about
55 KB.  So Option B cannot carry the first draft's 192 KB of Z80 memory: 64 KB plus two 16 KB
banks on the C6 (one on the C3) is what fits beside SSH.  Partition scheme: Huge APP (3 MB).

---

## 2. Video output options

### Option 3 (the one to build): RP2350-Zero HDMI VT100 card, `VIDEO_BACKEND_UART_RP2350_HDMI`
The Beetle prints to UART1 (`TX`/`RX` of the board variant: C6 GPIO 16/17, C3 GPIO 21/20) at
921600 8N1; the RP2350-Zero runs [`video_backends/rp2350_hdmi_vt100_card.c`](file:///home/ccd/retro_pc_project/02_beetle_terminal_os/video_backends/rp2350_hdmi_vt100_card.c):
an 80x30 colour ANSI terminal rendered line by line in the HSTX DMA interrupt
(`common/hstx_dvi.c`, from the pico-examples DVI encoder; 8x16 cells from the public-domain
font8x8; UART drained by interrupt into a 4 KB ring).  Compiles to a 12 KB image for
`waveshare_rp2350_zero` and a UF2; not yet run on hardware.  Wiring: Beetle `TX` -> Zero
GPIO 1, 5 V and ground; HDMI as in project 1 (Pico DVI Sock order, Type D pin numbers).
The Beetle spends nothing on video, and the same cell grid is what project 5's screen reader
reads.

### Option 1: 2-resistor composite, `VIDEO_BACKEND_COMPOSITE_SPI` (not working)
The first draft generated each line in a 64 us timer interrupt with `SPI.writeBytes()`, which
takes a FreeRTOS mutex (illegal in an interrupt) and blocks for 53 of every 64 us; on a
single-core C3/C6 the BLE and Wi-Fi stacks starve.  A working version needs a DMA-fed
hardware stream of the whole frame including sync, a different project.  The file is now a
documented stub that keeps the VT100 text model and a real font (`#error` unless
`COMPOSITE_EXPERIMENTAL`).

### Option 2: FabGL VGA, `VIDEO_BACKEND_VGA_FABGL`
Works only on the classic dual-core ESP32 with arduino-esp32 2.0.x, and that Beetle (DFR0575)
is 35 x 34 mm: fine for a desk build, not for the AirTag.  Pins 22/21/19/18/5 are the usual
FabGL set; DFR0575 breaks out only D2, D3, D4, D7, A0-A3, SDA, SCL, RX, TX, so even there a
check of the pinout image is needed.

---

## 3. OS Option A: FreeRTOS micro-shell ([`beetle_rtos_ssh_terminal.ino`](file:///home/ccd/retro_pc_project/02_beetle_terminal_os/os_freertos_shell/beetle_rtos_ssh_terminal.ino))
`wifi scan | wifi join <ssid> <pass> | wifi status`, `ssh <user>@<host> [port]` (LibSSH-ESP32,
vt100 80x25 PTY), `telnet <host> [port]` (Ctrl+] exits), `ls cat rm ed`, `ble free clear reboot`.
BLE keyboard via NimBLE-Arduino 2.x (bonding, boot and report notifications), HID to ASCII/VT100
with Ctrl, Shift, arrows, Home/End/Delete/PgUp/PgDn, and auto-repeat (400 ms, 30 Hz), which the
first draft lacked.  Known gap: the SSH client does not verify the server's host key yet.

## 4. OS Option B: CP/M-style host shell ([`beetle_cpm22_ssh.ino`](file:///home/ccd/retro_pc_project/02_beetle_terminal_os/os_cpm22_runcpm/beetle_cpm22_ssh.ino))
An `A>` command processor with `DIR TYPE ERA PIP CLS`, drives A: (LittleFS `/A0`) and B:/M:
(a RAM disk in the expanded pool), a 16 KB bank window at 8000h selected by `OUT (0F0h),A`
or `BANK n`, and the four utilities `SSH.COM TELNET.COM WIFI.COM BANK.COM` (generated by
[`make_cpm_tools.py`](file:///home/ccd/retro_pc_project/02_beetle_terminal_os/os_cpm22_runcpm/make_cpm_tools.py),
run with `uv run`; verified under z80pack `cpmsim` after fixing a wrong message address).
They call host BDOS functions 220-223 with DE pointing at the command tail.

**It is not a Z80 emulator.**  Its interpreter knows the eleven opcodes those utilities use
and refuses anything else with a pointer to
[`runcpm_patch/README.md`](file:///home/ccd/retro_pc_project/02_beetle_terminal_os/os_cpm22_runcpm/runcpm_patch/README.md),
which carries a patch adding BDOS 220-224 to RunCPM (the real CP/M 2.2 for the ESP32),
verified at run time on RunCPM's Linux build with these utilities, and the steps to run
WordStar and friends on the Beetle: RunCPM + a LittleFS
filesystem abstraction + the four host handlers from this sketch.  Note that CP/M's CCP
upper-cases command tails, so `SSH` lower-cases the user and host and `WIFI` cannot join an
SSID with lower-case letters from the prompt (use `wifi join` in Option A once, the
credentials are shared in NVS).

---

## 5. Directory contents
- `video_backends/rp2350_hdmi_vt100_card.c`, `CMakeLists.txt`: the HDMI card (builds).
- `video_backends/video_uart_rp2350_hdmi.h`: Beetle side of Option 3.
- `video_backends/video_composite_spi.h`: stub, see above.  `video_vga_fabgl.h`: classic ESP32 only.
- `os_freertos_shell/beetle_rtos_ssh_terminal.ino`: Option A.
- `os_cpm22_runcpm/beetle_cpm22_ssh.ino`, `ssh_com_bdos_ext.h`, `make_cpm_tools.py`, `disk_A/*.COM`: Option B.
- `os_cpm22_runcpm/runcpm_patch/`: RunCPM extension patch, header and instructions.
- `REVIEW.md`: findings.
