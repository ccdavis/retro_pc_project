# Standalone RP2350-Zero Home Computer Emulators (`03_rp2350_standalone_emulators`)

**Target hardware:** Waveshare RP2350-Zero (23.5 × 18.0 mm), standalone or paired with a Beetle ESP32-C6/C3 wireless companion.  
**Systems evaluated:** Apple //e, Apple //c, IBM PCjr, IBM PC XT, Commodore 64, and TI-99/4A.

**Build status:** `dual_core_engine/` drives 640×480 HSTX DVI output (`common/hstx_dvi.c`) and compiles into UF2 images for all four target video architectures with the Pico SDK (~10.7 KB text, 267 KB BSS). Core 0 is currently a stub that accepts Beetle UART HID frames and displays a test pattern; CPU emulation and sound synthesis are not yet integrated into `dual_core_engine/`.

---

## 1. Dual-Core Scanline Architecture on the RP2350

On the RP2040, bit-banging DVI over PIO consumed an entire CPU core. On the RP2350, the hardware **High-Speed Serial Transmit (`HSTX`)** peripheral on GPIO 12–19 serializes 252 Mbps TMDS video in hardware via DMA, freeing Core 1 to act as a dedicated video and audio coprocessor:

1. **Core 0** emulates the system CPU (65C02, 6510, 8088, or TMS9900) and disk/keyboard I/O.
2. **Core 1** renders video one scanline at a time into a **2-scanline ping-pong DMA buffer (2.5 KB of SRAM)** just ahead of the HSTX DMA channel, avoiding a 150–300 KB full-screen framebuffer. Evaluating 8 to 32 sprites, priority masks, and collision flags for a 320-pixel line takes under 6 µs on a 150–252 MHz Cortex-M33.
3. **Internal SRAM vs. External PSRAM:**
   - Systems with up to 256 KB of RAM (**Apple //e, Apple //c, Commodore 64, TI-99/4A, and a 128–256 KB IBM PCjr / PC 5150**) fit inside the RP2350-Zero's built-in **520 KB SRAM** without external memory.
   - A full **640 KB IBM PC XT** (or multi-megabyte disk caches) requires soldering an 8-pin **APS6404L-3SQR (8 MB QSPI PSRAM)** chip to the underside pads.

```
         STANDALONE WAVESHARE RP2350-ZERO DUAL-CORE ARCHITECTURE (520 KB SRAM)
+-------------------------------------------------------------------------------------+
|                                                                                     |
|  +-----------------------------------+       +-----------------------------------+  |
|  | CORE 0 (150-252 MHz Cortex-M33)   |       | CORE 1 (150-252 MHz Cortex-M33)   |  |
|  | - Vintage CPU Emulation:          |       | - Scanline Video Coprocessor:     |  |
|  |   65C02 / 6510 / 8088 / TMS9900   |       |   * Apple II NTSC Hi-Res / DHR    |  |
|  | - VIA/CIA Timers & Floppy Disk    |       |   * PCjr TGA 16-Color / CGA       |  |
|  |   Controller (IWM / 1541 / FDC)   |       |   * VIC-II / TMS9918A Sprites     |  |
|  | - Input: Beetle UART/PS2 Bridge   |       | - Audio DSP (FPU):                |  |
|  |   or TinyUSB Wireless Dongle Host |       |   * SID / Mockingboard / SN76489  |  |
|  +-----------------+-----------------+       +-----------------+-----------------+  |
|                    |                                           |                    |
|                    v             AHB5 Crossbar                 v                    |
|  +-------------------------------------------------------------------------------+  |
|  |           SHARED INTERNAL SRAM (520 KB: 8 x 64 KB + 2 x 4 KB banks)           |  |
|  |  [Emulated System RAM: 64K-256K]  [Floppy Track Cache]  [2.5 KB Line Buffer]  |  |
|  +-------------------------------------------------------------------------------+  |
|                                                                | DMA                |
|                                                                v                    |
|                                              +-----------------------------------+  |
|                                              | Hardware HSTX Block (GPIO 12..19) |  |
|                                              | 640x480@60Hz HDMI + Digital Audio |  |
|                                              +-----------------------------------+  |
+-------------------------------------------------------------------------------------+
```

---

## 2. System-by-System Comparison on the RP2350-Zero

| System | CPU & Speed | Video & Sound Hardware | Emulated RAM | Fits in 520 KB SRAM? | Existing RP2350 Projects |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **Apple //e** (Enhanced) | 65C02 @ 1.02 MHz | Text, Lo-Res, Hi-Res, Double Hi-Res (560×192) + Dual Mockingboard (6-voice AY-3-8910) | 128 KB (64K Main + 64K Aux) | Yes (with room for a 140 KB `.DSK` image in RAM) | [`rh1tech/frank-apple`](https://github.com/rh1tech/frank-apple) |
| **Apple //c** | 65C02 @ 1.02 MHz | Built-in 128K, DHR graphics, IWM disk, serial/mouse ports | 128 KB | Yes | [`ajtaji/AppleIIcEmulatorPicoSeries`](https://github.com/ajtaji/AppleIIcEmulatorPicoSeries) |
| **IBM PCjr** (Tandy 1000) | 8088 @ 4.77 MHz | TGA (320×200 16-color, shared with main RAM) + SN76489 3-voice PSG | 128–256 KB | Yes | [`xrip/pico-286`](https://github.com/xrip/pico-286), [`ajtaji/8086EmulatorPicoSeries`](https://github.com/ajtaji/8086EmulatorPicoSeries) |
| **IBM PC XT** | 8088 @ 4.77–8 MHz | CGA (320×200 / 640×200) or Hercules + AdLib (YM3812 OPL2) / PC Speaker | 640 KB | 200 KB conventional RAM without PSRAM (`pico-286` virtual memory mode); 8 MB PSRAM recommended | [`xrip/pico-286`](https://github.com/xrip/pico-286), [`DnCraptor/pc`](https://github.com/DnCraptor/pc) |
| **Commodore 64** | 6510 @ 0.985 / 1.02 MHz | VIC-II (8 sprites, raster IRQs, badlines, scrolling) + MOS 6581/8580 SID | 64 KB RAM + 1 KB Color RAM + 16 KB ROMs | Yes (~140 KB SRAM total) | [`rh1tech/frank-c64`](https://github.com/rh1tech/frank-c64) |
| **TI-99/4A** | TMS9900 @ 3.0 MHz | TMS9918A VDP (32 sprites, 16 colors) + TMS9919 Sound + TMS5200 Speech | 48.5 KB (16K VDP + 32K Expansion + 256B CPU) | Yes (~160 KB SRAM; PSRAM optional for large `.rpk` / disk cache) | [`PicoPlus-devel/pico-994A`](https://github.com/PicoPlus-devel/pico-994A) |

---

## 3. System Notes

### A. Apple //e & Apple //c
- Both the Enhanced Apple //e (with Extended 80-Column Card) and the Apple //c use **128 KB of RAM** (`Main` and `Aux` 64 KB banks).
- A standard Apple II 5.25" Disk II image (`.DSK` or `.PO`) is **140 KB** (`35 tracks × 16 sectors × 256 bytes`), so 128 KB of system RAM + 32 KB ROM + a 140 KB floppy image fit inside the 520 KB SRAM simultaneously.
- In [`rp2350_hstx_beamracer.h`](dual_core_engine/rp2350_hstx_beamracer.h), Core 1 renders Apple II Hi-Res (`280×192`, including white adjacent-pixel artifacting and high-bit palette phase shift) and Double Hi-Res (`560×192`) scanlines scaled to 640×480.

### B. IBM PCjr vs. IBM PC XT
- Unlike a PC XT with a separate 16 KB CGA card, the **IBM PCjr's Video Gate Array (TGA)** shares a 16 KB or 32 KB window inside main system RAM and pairs it with a built-in **TI SN76489** 3-voice + noise sound chip at port `0xC0`. A 128–256 KB PCjr fits in the RP2350-Zero's internal SRAM and supports 320×200 16-color graphics and 3-voice audio.
- For an **IBM PC XT**, `pico-286` without PSRAM provides **200 KB** of conventional RAM on the RP2350 (enough to boot DOS and run early 1981–84 software). Soldering an **8 MB QSPI PSRAM** (`APS6404L-3SQR`) enables the full **640 KB + EMS + AdLib** configuration. Note that upstream `pico-286` targets Murmulator boards (HDMI base pin 6, PSRAM on 18–21) and requires remapping those pins for the RP2350-Zero.

### C. Commodore 64 & TI-99/4A Scanline Renderers
- **TI-99/4A (`TMS9918A`):** Reads the isolated 16 KB VDP RAM. On each active scanline (`0..191`), Core 1 draws the Graphics I or Graphics II tile backdrop and border from VDP registers `R0–R7`, walks the 32-entry Sprite Attribute Table, draws up to 4 sprites on the line (8×8 or 16×16, normal or magnified), and updates the fifth-sprite and coincidence status flags. Text and Multicolor modes are not yet implemented in `rp2350_hstx_beamracer.h`.
- **Commodore 64 (`VIC-II`):** Renders 40-column character mode using the screen and character base addresses in `$D018` and the active VIC bank (falling back to the character ROM where mapped), and composites up to 8 sprites with sprite-to-sprite and sprite-to-background collision bitmasks. Multicolor modes, bitmap mode, and sprite X/Y expansion are not yet implemented in `rp2350_hstx_beamracer.h`.

---

## 4. Wireless Keyboard and Connectivity Options

### Option A: Beetle ESP32-C6/C3 Companion Inside the Case
Stacking the Beetle ESP32-C6/C3 with the RP2350-Zero using the 4-wire link from [`01_ti994a_emulator`](../01_ti994a_emulator/PLAN.md) provides wireless input and network storage without an external USB OTG splitter:
1. **Bluetooth Keyboard & Gamepad Bridge:** The Beetle pairs with a BLE keyboard/gamepad and outputs both `115200` baud UART HID frames (to RP2350 `GPIO 9`, `UART1 RX`) and **PS/2 clock & data** (`GPIO 6 & 7`). PS/2-native emulators (`frank-apple`, `frank-c64`, `pico-286`) use GPIO 6/7 (with their pin config edited to match), while UART-bridged builds (`pico-994A`, `standalone_emulator_main.c`) read `UART1`.
2. **Wi-Fi Disk Upload (`0xAA 0x55 0x02` UART blocks):** Can stream `.DSK`, `.D64`/`.PRG`, or `.IMG` disk images from a browser into RP2350 SRAM over UART.
3. **Virtual Serial Modem:** Maps `UART0` (`GPIO 0/1`) to the emulated serial port (`Super Serial Card`, `COM1:`, or C64 User Port) for Telnet/SSH dialing.

### Option B: Standalone RP2350-Zero with USB-C OTG Splitter + Dongle
Use a single RP2350-Zero with an external USB-C OTG power + USB-A splitter cable and a 2.4 GHz or Bluetooth HID USB nano-receiver, read via the RP2350's built-in **TinyUSB Host (`tuh_hid`)** stack.

### Option C: Standalone RP2350-Zero with Raspberry Pi Radio Module 2 (`RM2`)
Wire a **Raspberry Pi RM2 (`CYW43439`, 16.5 × 14.5 × 2.5 mm)** to 4 spare GPIO pads on the RP2350-Zero to run `BTstack` directly on the RP2350.

---

## 5. RP2350-Zero Pinout

| RP2350-Zero Pin | Connected Component | Function |
| :---: | :--- | :--- |
| **USB-C Port** | 5V USB power cable (or OTG splitter in Option B) | Powers the RP2350-Zero (and the Beetle via `5V` & `GND` in Option A) |
| **GPIO 8 & GPIO 9** | Beetle `RX` & `TX` (Option A) | `UART1` (`115200` baud): BLE keyboard / joystick HID frames (`GPIO 0/1` stay free for console `UART0`) |
| **GPIO 6 & GPIO 7** | Beetle `GPIO 4` & `GPIO 5` (Option A) | PS/2 clock & data for `frank-apple` / `frank-c64` (edit `board_config.h`; stock M2 uses GPIO 2/3) |
| **GPIO 2, 3, 4, 5** | MicroSD card slot | `SPI0 SCK` (GP2), `MOSI` (GP3), `MISO` (GP4), `CS` (GP5) |
| **GPIO 20** (optional) | `APS6404L-3SQR` `CS#` | Optional 8 MB QSPI PSRAM |
| **GPIO 12 & 13** | Micro-HDMI Type D pins 9 & 11 (270 Ω) | **HSTX Data 0 + / -** |
| **GPIO 14 & 15** | Micro-HDMI Type D pins 12 & 14 (270 Ω) | **HSTX Clock + / -** |
| **GPIO 16 & 17** | Micro-HDMI Type D pins 3 & 5 (270 Ω) | **HSTX Data 2 + / -** |
| **GPIO 18 & 19** | Micro-HDMI Type D pins 6 & 8 (270 Ω) | **HSTX Data 1 + / -** (Pico DVI Sock order; `common/hstx_dvi.c` also supports `frank-apple` M2 order via `HSTX_DVI_PINOUT_M2`) |
