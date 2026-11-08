# Review of `01_ti994a_emulator` (2026-11-07)

Checked against: DFRobot wiki pages for the Beetle ESP32-C6 (DFR1117), ESP32-C3 (DFR0868)
and classic ESP32 (DFR0575); arduino-esp32 `variants/dfrobot_beetle_esp32c{3,6}/pins_arduino.h`;
NimBLE-Arduino 2.x headers; `PicoPlus-devel/pico-994A` and `pico_shared` sources
(`BoardConfigs.cmake`, `hid_app.cpp`, `gamepad.h`, `main.cpp`); `rh1tech/frank-apple`
`board_config.h`; the pico-examples HSTX DVI encoder.  The Arduino sketch **compiles for both
Beetles** with arduino-esp32 3.3.12 and NimBLE-Arduino 2.5.2 (`tools/compile_sketch.sh`): 1.43 MB
flash / 51 KB RAM on the C6, 1.27 MB / 46 KB on the C3; it needs the Huge APP partition scheme.

| # | Finding | Fix |
| :-- | :-- | :-- |
| 1 | Plan: Beetle "512 KB RAM, 2 MB flash" for all variants; classic Beetle ESP32 listed at 25 x 20.5 mm | C6: 512 KB / 4 MB; C3: 400 KB / 4 MB; classic DFR0575 is 35 x 34 mm and does not fit. Plan rewritten. |
| 2 | Plan: Beetle "fits (verified)" although its own table shows a 32.3 mm diagonal against a 31.9 mm outer diameter | Stated as the central risk with the cavity numbers; corners trimmed or a taller shell. |
| 3 | Sketch: UART on GPIO 16/17, which do not exist on the ESP32-C3 | Uses the variant's `TX`/`RX` constants (C6 16/17, C3 21/20). |
| 4 | Sketch: NimBLE-Arduino 1.x API (`NimBLEAdvertisedDeviceCallbacks`, `start(0, nullptr, false)`, `onDisconnect(client)`) does not compile with 2.x, which arduino-esp32 3.x / the C6 require | Ported to 2.x (`NimBLEScanCallbacks`, `const NimBLEAdvertisedDevice*`, `start(ms, continue, restart)`, `onDisconnect(client, reason)`). |
| 5 | Sketch: kept a pointer to the scan result object past the scan | Copies the `NimBLEAddress` and connects by address. |
| 6 | Sketch: web pad FCTN+9/8/7/6/4/3 sent HID codes 42/41/40/39/21/20 = Backspace, Esc, Enter, '0', R, Q | Digits are 30..39: now 38/37/36/35/33/32. |
| 7 | Sketch: PS/2 output lacked right Shift/Ctrl/Alt, `[ ] \ \`` and the navigation keys, and never checked host inhibit | Added; waits while the host holds CLK low. |
| 8 | `uart_hid_bridge.h` called `process_kbd_report()`, which does not exist in pico-994A, and nothing in pico-994A ever called the header | Rewritten as `uart_hid_bridge.cpp` writing `io::getCurrentKeyboardState()` and gamepad 0 (what pico-994A's own USB code does); `pico994a_uart_hid.patch` adds the two calls and the source (verified with `git apply --check`); syntax-checked against `pico_shared/gamepad.h`. |
| 9 | Build script: `HW_CONFIG=2` with no explanation, `PICO_PLATFORM=rp2350`, no patch applied, shallow clone | Kept `HW_CONFIG=2`, which turns out to be the PicoPlus HSTX configuration matching this wiring (HW_CONFIG 6, "RP2350-Zero with custom PCB", is PIO DVI on GPIO 6-13 and has no HSTX branch); added `USE_HSTX=1`, Release build, NES pad and LED pins disabled, `rp2350-arm-s`, the bridge patch, recursive clone. **Builds: `pico994A.uf2`, 447 KB text, 251 KB bss.** The Debug build fails to link upstream (`SAMS_SwapBank`). |
| 10 | UART link on GPIO 0/1 collides with pico-994A's console and serial-keyboard feature on UART0 | Link moved to UART1, GPIO 8 (TX) / 9 (RX). |
| 11 | HDMI table: frank-apple "M2" lane order (clock on 12/13, negative first) presented as the pico-994A order, with Type A connector pin numbers for a Micro-HDMI Type D socket | Pico DVI Sock / PicoPlus order (D0 12/13, CK 14/15, D2 16/17, D1 18/19, positive first), Type D pin numbers, and the board_config.h edit for frank-apple. |
| 12 | PS/2 on RP2350 GPIO 6/7 "works out of the box" with frank-apple | frank-apple M1 uses 0/1, M2 uses 2/3 (the SD pins here); the edit is documented. |
| 13 | SD wiring | Matches HW_CONFIG 6 exactly (CS 5, SCK 2, MOSI 3, MISO 4); kept. |
| 14 | Optional PSRAM CS on GPIO 8 | Now the UART1 TX pin; PSRAM CS moved to GPIO 20 in the plan. |

| 15 | The RP2350-Zero's WS2812 LED is on GPIO 16, an HSTX lane | Documented as a signal-integrity caveat. |

Not done: running anything on hardware.
