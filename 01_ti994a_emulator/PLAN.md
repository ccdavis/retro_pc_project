# TI-99/4A Console Emulator Plan (Beetle ESP32-C6/C3 + RP2350-Zero)

**Primary target:** Texas Instruments TI-99/4A (`PicoPlus-devel/pico-994A`, `HW_CONFIG 2` with HSTX: the PicoPlus configuration whose HDMI and SD pins match this wiring; `HW_CONFIG 6` uses PIO-DVI on GPIO 6–13).  
**Other compatible emulators:** Apple //e (`rh1tech/frank-apple`), Commodore 64 (`rh1tech/frank-c64`), ZX Spectrum (`fruit-bat/pico-zxspectrum`), with the pin-config adjustments in Section 4.

**Build status:** The Beetle sketch compiles for both the ESP32-C6 and ESP32-C3 (`arduino-esp32` 3.3.12, `NimBLE-Arduino` 2.5.2, using the "Huge APP (3 MB No OTA)" partition scheme for the 1.4 MB image), and `pico-994A` builds for the RP2350-Zero with the UART keyboard bridge patch applied (`build_pico994a.sh`: `HW_CONFIG 2`, `USE_HSTX=1`, Release; 447 KB flash). Hardware testing is still pending.

---

## 1. Physical Constraints

An Apple AirTag is 31.9 mm in outer diameter and 8.0 mm thick; the plastic shell and steel back plate leave an internal cavity roughly 29 mm across and 6.5 mm deep around the CR2032 cell (20 × 3.2 mm). A rectangular board fits flat only if its diagonal is below the cavity diameter or its corners are trimmed.

| Board | Size | Diagonal | Fit |
| :--- | :--- | :--- | :--- |
| Seeed XIAO (RP2350 / ESP32-C6) | 21.0 × 17.8 mm | 27.5 mm | Fits |
| Pimoroni Tiny 2350 | 22.9 × 18.0 mm | 29.1 mm | Tight |
| **Waveshare RP2350-Zero** | 23.5 × 18.0 mm | 29.6 mm | Tight; the USB-C socket (3.2 mm tall) must sit in a rim cutout |
| **DFRobot Beetle ESP32-C6 / ESP32-C3** | 25.0 × 20.5 mm | 32.3 mm | Exceeds the 31.9 mm outer diameter; requires trimming the corners ~2 mm per side or using a wider shell |
| DFRobot Beetle ESP32 (classic, DFR0575) | 35.0 × 34.0 mm | 48.8 mm | Does not fit |
| Sipeed Tang Primer 25K core board | 23.0 × 18.0 mm | 29.2 mm | Tight; requires a carrier board |
| Raspberry Pi Pico 2 W | 51.0 × 21.0 mm | 55.2 mm | Does not fit |

Stacking two boards with USB-C sockets plus a Micro-HDMI socket within 6.5 mm of depth is tight; a 3D-printed AirTag-style shell a few millimeters taller is a practical fallback.

---

## 2. Architecture: Dual Boards with a Low-Bandwidth Link

1. **Board 1 — Beetle ESP32-C6 (DFR1117: 512 KB SRAM, 4 MB flash, 13 GPIOs) or Beetle ESP32-C3 (DFR0868: 400 KB SRAM, 4 MB flash, 13 GPIOs)**, single-core RISC-V at 160 MHz. Acts as a BLE HID host for a Bluetooth keyboard, forwards keys as:
   - **UART HID frames** (13 bytes: `AA 55 01 mod res k0..k5 joy xor`) to the RP2350's `UART1`,
   - **PS/2 device output** (bit-banged Set 2 scancodes) for PS/2-native emulators,
   and serves a Wi-Fi web page with the TI-99/4A `FCTN` keys (`http://airtag-ti99.local`).
2. **Board 2 — Waveshare RP2350-Zero (RP2350A, 520 KB SRAM = 8×64 KB + 2×4 KB, 4 MB flash, HSTX on GPIO 12–19)** runs `pico-994A`: TMS9900 CPU, TMS9918A VDP, TMS9919 sound, 32 KB expansion RAM, TI BASIC, cassette, TMS5200 speech, and 640×480 HDMI output with audio.

---

## 3. Bill of Materials

| Part | Purpose | Notes |
| :--- | :--- | :--- |
| Beetle ESP32-C6 or ESP32-C3 | BLE host, Wi-Fi pad | 25.0 × 20.5 mm |
| Waveshare RP2350-Zero | Emulator + HDMI | ~$4 |
| Micro-HDMI Type D socket or FPC breakout | HDMI output | Pin numbering below is Type D |
| 8 × 270 Ω 0402/0603 resistors | TMDS series termination | |
| MicroSD socket | Cartridges and disk images | `pico-994A` loads ROMs from SD |
| APS6404L-3SQR 8 MB QSPI PSRAM (optional) | `pico-994A` disk cache, `.rpk` > 32 KB | Use GPIO 20 for `CS#` to avoid colliding with `UART1` on GPIO 8 |
| 30 AWG wire, Kapton tape | Interconnects and insulation | |

---

## 4. Wiring

### A. Power
Beetle `VIN` (5 V) ↔ RP2350-Zero `5V`, `GND` ↔ `GND`.

### B. Beetle ↔ RP2350-Zero
| Beetle Pin | Signal | RP2350-Zero Pin | Notes |
| :--- | :--- | :--- | :--- |
| `TX` (C6: GPIO 16, C3: GPIO 21) | UART HID frames, 115200 8N1 | **GPIO 9 (UART1 RX)** | `pico-994A` uses `UART0` (GPIO 0/1) for its console and serial keyboard feature, so the HID link uses `UART1` |
| `RX` (C6: GPIO 17, C3: GPIO 20) | Return channel (unused) | **GPIO 8 (UART1 TX)** | |
| GPIO 4 | PS/2 clock | GPIO 6 | Used by `frank-apple` / `frank-c64` / `pico-zxspectrum` with the pin edit in Section 4E |
| GPIO 5 | PS/2 data | GPIO 7 | |

The sketch takes its UART pins from the board variant (`TX`/`RX` in `pins_arduino.h`), so the same source builds for both the C6 and C3.

### C. MicroSD (SPI0, matching `pico-994A` `HW_CONFIG` 2 and 6)
| MicroSD | RP2350-Zero |
| :--- | :--- |
| CS | GPIO 5 |
| MOSI (DI) | GPIO 3 |
| SCK | GPIO 2 |
| MISO (DO) | GPIO 4 |
| VDD / VSS | 3V3 / GND |

### D. HDMI (HSTX GPIO 12–19, Pico DVI Sock Order)
This is the lane order used by `pico-examples` and the PicoPlus HSTX configurations (`pico-994A` `HW_CONFIG 2`): **D0 on 12/13, clock on 14/15, D2 on 16/17, D1 on 18/19, positive pin first.**

Note: the RP2350-Zero's onboard WS2812 status LED is connected to GPIO 16 (`TMDS D2+`). Its data input adds a small capacitive load on that lane and causes the LED to flicker during video output; if needed, the LED can be desoldered.

| RP2350-Zero | 270 Ω | TMDS Signal | Micro-HDMI Type D Pin |
| :--- | :--- | :--- | :--- |
| GPIO 12 | Yes | Data 0 + | 9 |
| GPIO 13 | Yes | Data 0 - | 11 |
| GPIO 14 | Yes | Clock + | 12 |
| GPIO 15 | Yes | Clock - | 14 |
| GPIO 16 | Yes | Data 2 + | 3 |
| GPIO 17 | Yes | Data 2 - | 5 |
| GPIO 18 | Yes | Data 1 + | 6 |
| GPIO 19 | Yes | Data 1 - | 8 |
| GND | | Shields | 4, 7, 10, 13, 16 |
| 5V via Schottky | | +5 V | 19 |
| (leave open) | | Hot plug detect | 1 |

Type D pin map: 1 HPD, 2 utility, 3 D2+, 4 D2 shield, 5 D2-, 6 D1+, 7 D1 shield, 8 D1-, 9 D0+, 10 D0 shield, 11 D0-, 12 CK+, 13 CK shield, 14 CK-, 15 CEC, 16 DDC/CEC ground, 17 SCL, 18 SDA, 19 +5 V. If using an FPC breakout, follow the silkscreen labels on the breakout board.

### E. Building Other Emulators for This Wiring
- **pico-994A:** Build with `-DHW_CONFIG=2 -DUSE_HSTX=1 -DPICO_BOARD=waveshare_rp2350_zero` plus `-DNES_*=-1 -DLED_GPIO_PIN=-1` (the NES pad pins otherwise claim GPIO 6–11). Keyboard input uses `uart_hid_bridge.cpp` (Section 5).
- **frank-apple / frank-c64 (M2 layout):** Edit `src/board_config.h` under `BOARD_M2`: HDMI `CLKP 14, CLKN 15, D0P 12, D0N 13, D1P 18, D1N 19, D2P 16, D2N 17`; SD `CLK 2, CMD 3, D0 4, D3 5`; `PS2_PIN_CLK 6, PS2_PIN_DATA 7` (stock M2 puts the clock on 12/13, SD on 4–7, and PS/2 on 2/3).
- **pico-zxspectrum:** Set the DVI pin base and PS/2 pins in its CMake configuration to match the table above.

---

## 5. Keyboard Path into `pico-994A`

`pico-994A`'s USB stack writes keyboard state into `io::getCurrentKeyboardState()` (modifier byte + 6 keycodes), which the emulator polls each frame. [`rp2350_pico994a/uart_hid_bridge.cpp`](file:///home/ccd/retro_pc_project/01_ti994a_emulator/rp2350_pico994a/uart_hid_bridge.cpp) drains `UART1` via interrupt, parses the 13-byte frames from the Beetle, and populates `io::getCurrentKeyboardState()` and `io::getCurrentGamePadState(0)`.

[`build_pico994a.sh`](file:///home/ccd/retro_pc_project/01_ti994a_emulator/rp2350_pico994a/build_pico994a.sh) copies the bridge into the tree, applies [`pico994a_uart_hid.patch`](file:///home/ccd/retro_pc_project/01_ti994a_emulator/rp2350_pico994a/pico994a_uart_hid.patch) (adding init and poll calls in `main.cpp`), and builds `pico994A.uf2`.

As an unpatched fallback, `pico-994A` also has a built-in serial keyboard mode (enabled in its settings menu) that accepts plain ASCII on `UART0` at 115200 baud with XON/XOFF, though that mode is intended for pasting BASIC listings and does not support full matrix key scanning.

---

## 6. TI-99/4A Key Map (`pico-994A`, Left Alt = `FCTN`)

| TI-99/4A Function | TI Keys | Bluetooth Keyboard | Web Pad `k(mod,hid)` |
| :--- | :--- | :--- | :--- |
| Cursor | FCTN E/X/S/D | Arrow keys | 82 / 81 / 80 / 79 |
| Delete | FCTN 1 | Backspace or Delete | 4, 30 |
| Insert | FCTN 2 | Insert / Alt+2 | 4, 31 |
| Erase / Clear | FCTN 3 / 4 | Alt+3 / Alt+4 | 4, 32 / 4, 33 |
| Proceed | FCTN 6 | Alt+6 | 4, 35 |
| Aid / Redo / Back | FCTN 7 / 8 / 9 | Alt+7 / 8 / 9 | 4, 36 / 4, 37 / 4, 38 |
| Quit | FCTN = | Alt+= | 4, 46 |
| Emulator menu | | F1 | 0, 58 |
