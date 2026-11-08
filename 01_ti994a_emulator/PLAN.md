# AirTag Retro Computer Plan (Option A: Beetle ESP32 + RP2350-Zero)
**Primary target:** Texas Instruments TI-99/4A (`PicoPlus-devel/pico-994A`, HW_CONFIG 2 with HSTX: the PicoPlus configuration whose HDMI and SD pins match this wiring; its HW_CONFIG 6 "RP2350-Zero with custom PCB" is a PIO-DVI pinout on GPIO 6-13)
**Also:** Apple //e (`rh1tech/frank-apple`), Commodore 64 (`rh1tech/frank-c64`), ZX Spectrum (`fruit-bat/pico-zxspectrum`), with a pin-config edit each (section 4).

**Review status (2026-11-07):** facts below were checked against the vendor pages,
the board variant files of arduino-esp32 and the emulator sources; see `REVIEW.md`
for what the first draft got wrong.  The Beetle sketch compiles for both Beetle variants
(arduino-esp32 3.3.12, NimBLE-Arduino 2.5.2; select the "Huge APP (3 MB No OTA)" partition
scheme, the image is 1.4 MB), and pico-994A builds for the RP2350-Zero with the keyboard
bridge patch applied (`build_pico994a.sh`: HW_CONFIG 2, HSTX, Release; 447 KB of flash).
Nothing has been run on hardware yet.

---

## 1. Physical constraints

An Apple AirTag is 31.9 mm in diameter and 8.0 mm thick *outside*; the plastic
shell and the steel back plate make the cavity smaller (roughly 29 mm across and
6.5 mm deep around the CR2032, which is 20 x 3.2 mm).  A rectangular board fits
flat only if its diagonal is below the cavity diameter, or its corners are
trimmed.

| Board | Size | Diagonal | Fit |
| :-- | :-- | :-- | :-- |
| Seeed XIAO (RP2350 / ESP32-C6) | 21.0 x 17.8 mm | 27.5 mm | yes |
| Pimoroni Tiny 2350 | 22.9 x 18.0 mm | 29.1 mm | tight |
| **Waveshare RP2350-Zero** (chosen) | 23.5 x 18.0 mm | 29.6 mm | tight; the USB-C socket (3.2 mm tall) must sit in a rim cutout |
| **DFRobot Beetle ESP32-C6 / ESP32-C3** (your friend's board) | 25.0 x 20.5 mm | 32.3 mm | **exceeds the outer diameter**; it only fits if the corners are cut back ~2 mm per side or the shell is opened up. Measure before committing. |
| DFRobot Beetle ESP32 (classic, DFR0575) | 35 x 34 mm | 48.8 mm | **no** (the first draft listed it at 25 x 20.5 mm) |
| Sipeed Tang Primer 25K core board | 23 x 18 mm | 29.2 mm | tight, needs a carrier |
| Raspberry Pi Pico 2 W | 51 x 21 mm | 55.2 mm | no |

Two boards with USB-C sockets stacked plus a Micro-HDMI socket in 6.5 mm of
depth is the hardest part of this build; a 3D-printed AirTag-shaped shell a few
millimetres taller is the realistic fallback.

---

## 2. Architecture: two boards, low-bandwidth link

1. **Board 1, Beetle ESP32-C6 (DFR1117: 512 KB SRAM, 4 MB flash, 13 GPIOs) or
   Beetle ESP32-C3 (DFR0868: 400 KB SRAM, 4 MB flash, 13 GPIOs)**, single-core
   RISC-V at 160 MHz.  BLE HID host for any Bluetooth keyboard; forwards keys as
   - **UART HID frames** (13 bytes: `AA 55 01 mod res k0..k5 joy xor`) to the
     RP2350's UART1,
   - **PS/2 device output** (bit-banged Set 2 scancodes) for PS/2-native
     emulators,
   and serves a Wi-Fi web page with the TI-99/4A FCTN keys (`http://airtag-ti99.local`).
2. **Board 2, Waveshare RP2350-Zero (RP2350A, 520 KB SRAM = 8 x 64 KB + 2 x 4 KB,
   4 MB flash, HSTX on GPIO 12-19)** runs pico-994A: TMS9900, TMS9918A, TMS9919,
   32 K expansion, TI BASIC, cassette, TMS5200 speech, 640x480 HDMI with audio.

---

## 3. Bill of materials

| Part | Purpose | Note |
| :-- | :-- | :-- |
| Beetle ESP32-C6 or -C3 | BLE host, Wi-Fi pad | owned |
| Waveshare RP2350-Zero | emulator + HDMI | ~$4 |
| Micro-HDMI Type D socket or FPC breakout | HDMI out | pin numbering below is Type D |
| 8 x 270 ohm 0402/0603 | TMDS series resistors | |
| MicroSD socket | cartridges, disks | pico-994A needs the card for ROMs |
| APS6404L-3SQR 8 MB QSPI PSRAM (optional) | pico-994A disk cache, `.rpk` > 32 KB | CS on GPIO 8 would collide with the HID UART below; use GPIO 20 if fitted |
| 30 AWG wire, Kapton | | |

---

## 4. Wiring

### A. Power: Beetle `VIN` (5 V) <-> Zero `5V`, `GND` <-> `GND`.

### B. Beetle -> RP2350-Zero
| Beetle pin | Signal | RP2350-Zero pin | Notes |
| :-- | :-- | :-- | :-- |
| `TX` (C6: GPIO16, C3: GPIO21) | UART HID frames, 115200 8N1 | **GPIO 9 (UART1 RX)** | pico-994A already uses UART0 (GPIO 0/1) for its console and its own "serial keyboard" feature, so the HID link takes UART1 |
| `RX` (C6: GPIO17, C3: GPIO20) | return channel (unused) | **GPIO 8 (UART1 TX)** | |
| GPIO 4 | PS/2 clock | GPIO 6 | for frank-apple / frank-c64 / pico-zxspectrum with the pin edit in section 4E |
| GPIO 5 | PS/2 data | GPIO 7 | |

The sketch takes the UART pins from the board variant (`TX`/`RX` in
`pins_arduino.h`), so it is right on both Beetles.  GPIO 16/17, hard-coded in
the first draft, do not exist on the C3.

### C. MicroSD (SPI0, exactly pico-994A HW_CONFIG 2 and 6)
| MicroSD | RP2350-Zero |
| :-- | :-- |
| CS | GPIO 5 | 
| MOSI (DI) | GPIO 3 |
| SCK | GPIO 2 |
| MISO (DO) | GPIO 4 |
| VDD / VSS | 3V3 / GND |

### D. HDMI (HSTX GPIO 12-19), Pico DVI Sock order
This is the order pico-examples and the PicoPlus HSTX configurations (pico-994A
HW_CONFIG 2) use: **D0 on 12/13, clock on 14/15, D2 on 16/17, D1 on 18/19,
positive pin first.**  Caveat: the RP2350-Zero's WS2812 status LED hangs on GPIO 16,
which is TMDS D2+ here.  Its data input is a small capacitive load on that lane and the
LED will flicker; hobby HSTX builds on this board live with it, or remove the LED.  The first draft listed the frank-apple "M2" order (clock
on 12/13 with the negative pin first) and Type A connector pin numbers; a
Micro-HDMI Type D socket is numbered differently.

| RP2350-Zero | 270 ohm | TMDS signal | Micro-HDMI Type D pin |
| :-- | :-- | :-- | :-- |
| GPIO 12 | yes | Data 0 + | 9 |
| GPIO 13 | yes | Data 0 - | 11 |
| GPIO 14 | yes | Clock + | 12 |
| GPIO 15 | yes | Clock - | 14 |
| GPIO 16 | yes | Data 2 + | 3 |
| GPIO 17 | yes | Data 2 - | 5 |
| GPIO 18 | yes | Data 1 + | 6 |
| GPIO 19 | yes | Data 1 - | 8 |
| GND | | shields | 4, 7, 10, 13, 16 |
| 5V via Schottky | | +5 V | 19 |
| (leave open) | | Hot plug detect | 1 |

Type D pin map: 1 HPD, 2 utility, 3 D2+, 4 D2 shield, 5 D2-, 6 D1+, 7 D1 shield,
8 D1-, 9 D0+, 10 D0 shield, 11 D0-, 12 CK+, 13 CK shield, 14 CK-, 15 CEC,
16 DDC/CEC ground, 17 SCL, 18 SDA, 19 +5 V.  Breakout boards print the signal
names; trust the silkscreen over any table, including this one.

### E. Using the other emulators on this wiring
- **pico-994A**: `-DHW_CONFIG=2 -DUSE_HSTX=1 -DPICO_BOARD=waveshare_rp2350_zero` plus `-DNES_*=-1 -DLED_GPIO_PIN=-1` (the pad pins would claim GPIO 6-11); HDMI and SD pins match as built.  HW_CONFIG 6 is PIO DVI on GPIO 6-13 and would need different HDMI wiring.  Keyboard: `uart_hid_bridge.cpp` (section 5).
- **frank-apple / frank-c64 (M2 layout)**: edit `src/board_config.h` under `BOARD_M2`: HDMI `CLKP 14, CLKN 15, D0P 12, D0N 13, D1P 18, D1N 19, D2P 16, D2N 17`; SD `CLK 2, CMD 3, D0 4, D3 5`; `PS2_PIN_CLK 6, PS2_PIN_DATA 7`.  Stock M2 has the clock on 12/13, SD on 4-7 and PS/2 on 2/3, which collide with the SD card here.
- **pico-zxspectrum**: configure its DVI pin base and PS/2 pins the same way (its CMake exposes them).

---

## 5. Keyboard path into pico-994A

pico-994A has no `process_kbd_report()` (the first draft's header called one).
Its USB code fills `io::getCurrentKeyboardState()` (modifier + 6 keycodes) and
the emulator reads that structure every frame.  `rp2350_pico994a/uart_hid_bridge.cpp`
therefore drains UART1 from an interrupt, parses the 13-byte frames and writes
the same structure, plus the joystick byte into `io::getCurrentGamePadState(0)`.
`build_pico994a.sh` copies the file in, applies `pico994a_uart_hid.patch`
(two lines in `main.cpp`: init and poll), and builds HW_CONFIG 2 with HSTX; verified to
produce `pico994A.uf2`.

Fallback with no patch at all: pico-994A's **serial keyboard** (settings menu)
accepts ASCII on UART0 at 115200 with XON/XOFF; the Beetle could send plain
characters there.  It is meant for pasting listings and misses keys for programs
that scan the keyboard directly, so the HID bridge is the real solution.

---

## 6. TI-99/4A key map (pico-994A, Left Alt = FCTN)

| TI-99/4A | TI keys | Bluetooth keyboard | web pad `k(mod,hid)` |
| :-- | :-- | :-- | :-- |
| cursor | FCTN E/X/S/D | arrows | 82/81/80/79 |
| delete | FCTN 1 | Backspace or Delete | 4,30 |
| insert | FCTN 2 | Insert / Alt+2 | 4,31 |
| erase / clear | FCTN 3 / 4 | Alt+3 / Alt+4 | 4,32 / 4,33 |
| proceed | FCTN 6 | Alt+6 | 4,35 |
| aid / redo / back | FCTN 7 / 8 / 9 | Alt+7 / 8 / 9 | 4,36 / 4,37 / 4,38 |
| quit | FCTN = | Alt+= | 4,46 |
| emulator menu | | F1 | 0,58 |

The web pad's HID codes for FCTN 3, 4, 6, 7, 8 and 9 were wrong in the first
draft (they sent Q, R, 0, Enter, Esc and Backspace); the table above is what the
sketch sends now.
