# AirTag Standalone RP2350-Zero Retro Computer Plan (`03_rp2350_standalone_emulators`)
**Target Hardware:** Standalone **Waveshare RP2350-Zero** ($23.5 \times 18.0\text{ mm}$) inside an Apple AirTag Case  
**Candidate Systems Evaluated:** Apple //e, Apple //c, IBM PCjr, IBM PC XT, Commodore 64, and TI-99/4A

**Review status (2026-11-07):** `dual_core_engine/` now drives the HSTX DVI output for real
(`common/hstx_dvi.c`, from the pico-examples encoder) and compiles for all four machines with
the Pico SDK; it still contains **no CPU emulation** (Core 0 is a stub that services the Beetle
link).  The scanline renderers' coverage is listed honestly in the header.  Facts below were
corrected against the emulator sources; see `REVIEW.md`.

---

## 1. The Big Question: Are Sprites & Custom Graphics Chips Too Hard for the RP2350 Alone?

Your intuition about sprites and custom video/sound chips (like the C64's **VIC-II + SID** or the TI-99/4A's **TMS9918A**) is spot-on for *older* single-core microcontrollers—and even for the original RP2040, where the second CPU core was 100% consumed bit-banging HDMI pixels in software.

**On the RP2350, the bottleneck completely flips:**
1. **Hardware `HSTX` Frees Core 1:** Because the RP2350 has a dedicated hardware **High-Speed Serial Transmit (`HSTX`)** peripheral on `GPIO 12–19`, converting pixels into 252 Mbps HDMI TMDS signals happens in silicon with **0% CPU load**.
2. **Core 1 Becomes a Dedicated 150–252 MHz Video & Sound Coprocessor:**
   * While **Core 0** emulates the vintage CPU (`1 MHz 6502/6510`, `3 MHz TMS9900`, or `4.77 MHz 8088`) and disk/keyboard I/O,
   * **Core 1** runs a **"Beam-Racing" Scanline Renderer + Audio DSP** (assisted by the Cortex-M33's hardware single-precision FPU and DSP instructions).
3. **Why "Racing the Beam" Solves Both Sprites *and* SRAM Limits:**
   * A full 640×480 framebuffer would eat **150 KB to 300 KB of SRAM**—over half of the RP2350's 520 KB memory!
   * Instead, Core 1 renders video **one scanline at a time** into a tiny **2-scanline ping-pong DMA buffer (only 2.5 KB of SRAM!)** just ahead of the HDMI beam:
     * For **C64 (`VIC-II`)** or **TI-99/4A (`TMS9918A`)**, evaluating 8 or 32 hardware sprites, priority masks, scrolling, and pixel-collision flags for a single 320-pixel line takes **less than 6 microseconds** on a 150–252 MHz Cortex-M33!
   * **The Real Constraint on a Bare RP2350-Zero isn't Sprites—it's System RAM (`520 KB SRAM`):**
     * Systems with $\le 256\text{ KB}$ of emulated RAM (**Apple //e, Apple //c, C64, TI-99/4A, and a 128K–256K IBM PCjr / PC 5150**) fit **entirely inside the RP2350-Zero's built-in 520 KB SRAM** with no extra RAM chip needed!
     * Only a full **640 KB IBM PC XT** (or multi-megabyte disk caches) requires soldering the optional $1.50 8-pin **APS6404L-3SQR (8 MB QSPI PSRAM)** chip to the RP2350-Zero.

```
         STANDALONE WAVESHARE RP2350-ZERO DUAL-CORE ARCHITECTURE (520 KB SRAM)
+-------------------------------------------------------------------------------------+
|                                                                                     |
|  +-----------------------------------+       +-----------------------------------+  |
|  | CORE 0 (150-252 MHz Cortex-M33)   |       | CORE 1 (150-252 MHz Cortex-M33)   |  |
|  | - Vintage CPU Emulation:          |       | - Scanline Video Coprocessor:     |  |
|  |   65C02 / 6510 / 8088 / TMS9900   |       |   * Apple II NTSC Hi-Res / DHR    |  |
|  | - Via/CIA Timers & Floppy Disk    |       |   * PCjr TGA 16-Color / CGA       |  |
|  |   Controller (IWM / 1541 / FDC)   |       |   * VIC-II / TMS9918A Sprites     |  |
|  | - Input: Beetle UART/PS2 Bridge   |       | - Audio DSP (FPU):                |  |
|  |   OR TinyUSB Wireless Dongle Host |       |   * SID / Mockingboard / SN76489  |  |
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

## 2. System-by-System Feasibility on the Standalone RP2350-Zero

| System | CPU & Speed | Video & Sound Hardware | Emulated RAM Needed | Fits in Stock 520 KB SRAM? | Existing RP2350 Open-Source Projects |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **Apple //e** *(Enhanced)* | 65C02 @ 1.02 MHz | Text, Lo-Res, Hi-Res, **Double Hi-Res (560×192)** + **Dual Mockingboard** (6-voice AY-3-8910) | **128 KB** *(64K Main + 64K Aux 80-Col Card)* | **Yes (Easily!)** Even holds a full 140 KB `.DSK` floppy in RAM! | [`rh1tech/frank-apple`](https://github.com/rh1tech/frank-apple) |
| **Apple //c** | 65C02 @ 1.02 MHz | Built-in 128K, DHR graphics, IWM disk, serial/mouse ports | **128 KB** | **Yes (Easily!)** | [`ajtaji/AppleIIcEmulatorPicoSeries`](https://github.com/ajtaji/AppleIIcEmulatorPicoSeries) *(PureMetal SRAM core)* |
| **IBM PCjr** *(Tandy 1000)* | 8088 @ 4.77 MHz | **TGA (320×200 16-color)** *(shares system RAM!)* + **SN76489 3-voice + noise** PSG | **128 KB – 256 KB** | **Yes!** Because TGA video shares main RAM, no separate VRAM is needed. | [`xrip/pico-286`](https://github.com/xrip/pico-286) / [`ajtaji/8086EmulatorPicoSeries`](https://github.com/ajtaji/8086EmulatorPicoSeries) |
| **IBM PC XT** | 8088 @ 4.77–8 MHz | **CGA (320×200 / 640×200)** or **Hercules** + **AdLib (YM3812 OPL2)** / PC Speaker | **640 KB** | **Only 200 KB of conventional RAM without PSRAM** (`pico-286` "virtual memory" mode on RP2350); the project recommends an 8 MB PSRAM and targets Murmulator boards (HDMI base pin 6, PSRAM on 18-21), so the Zero needs its own pin configuration. | [`xrip/pico-286`](https://github.com/xrip/pico-286), [`DnCraptor/pc`](https://github.com/DnCraptor/pc) |
| **Commodore 64** | 6510 @ 0.985 / 1.02 MHz | **VIC-II** (8 hardware sprites, raster IRQs, badlines, scrolling) + **MOS 6581/8580 SID** | **64 KB RAM** + 1 KB Color RAM + 16 KB ROMs | **Yes (with Scanline Beam-Racing!)** *(Uses only ~140 KB SRAM total)*. | [`rh1tech/frank-c64`](https://github.com/rh1tech/frank-c64), `pico-c64` |
| **TI-99/4A** | TMS9900 @ 3.0 MHz | **TMS9918A VDP** (32 sprites, 16 colors) + **TMS9919 Sound** + **TMS5200 Speech** | **48.5 KB** *(16K VDP + 32K Expansion + 256B CPU)* | **Yes!** Core fits in ~160 KB SRAM; add $1.50 PSRAM if using `pico-994A`'s large `.rpk` / 3-drive cache. | [`PicoPlus-devel/pico-994A`](https://github.com/PicoPlus-devel/pico-994A) |

---

## 3. Deep Dive: Why Each System Shines on the RP2350-Zero

### A. Apple //e & Apple //c — The #1 Best Fit for a Bare RP2350-Zero
* **Zero External PSRAM Needed:** Both the Enhanced Apple //e (with Extended 80-Column Card) and the Apple //c max out at **128 KB of RAM** (Two 64 KB banks: `Main` and `Aux`).
* **Full Floppy Disk in Internal SRAM:** A standard Apple II 5.25" Disk II image (`.DSK` or `.PO`) is only **140 KB** (`35 tracks × 16 sectors × 256 bytes`). That means on a stock RP2350-Zero (`520 KB SRAM`), you can hold **128 KB of Apple //c RAM + 32 KB ROM + a complete 140 KB RWTS floppy disk image directly in internal SRAM**, with ~200 KB of SRAM still left over!
* **How Core 1 Handles Wozniak's Video & Mockingboard Audio:**
  * Apple II Hi-Res (`280×192`) and Double Hi-Res (`560×192`) generate color through NTSC phase-shift artifacting. Core 1 uses a fast 16-entry sliding bit-window lookup table per scanline to output authentic NTSC artifact color (or toggleable crisp RGB monochrome green/amber phosphor for 80-column text!) at 640×480 over HDMI.
  * With the FPU on Core 1, emulating **two Mockingboard sound cards** (six AY-3-8910 PSG channels + two 6522 VIAs + Apple 1-bit speaker clicks) into the HDMI audio stream takes <5% of Core 1's time.

### B. IBM PCjr vs. IBM PC XT — The Surprising PCjr Advantage
* **Why the IBM PCjr (and Tandy 1000) is awesome on a 520 KB RP2350-Zero:**
  * Unlike a PC XT with a separate CGA card (`16 KB VRAM`) or EGA/VGA card (`256 KB VRAM`), the **IBM PCjr's Video Gate Array (TGA)** has **no dedicated VRAM**—it shares a 16 KB or 32 KB window directly inside the PCjr's main **128 KB–256 KB RAM**!
  * Even better, the PCjr built in a **Texas Instruments SN76489 3-voice square-wave + noise sound chip** (the exact same sound chip inside the TI-99/4A and Sega Master System) at I/O port `0xC0`.
  * A **256 KB or 384 KB IBM PCjr** fits completely inside the RP2350-Zero's 520 KB internal SRAM and gives you **16-color 320×200 graphics + 3-voice chiptune audio** in DOS games (*King's Quest*, *Space Quest*, *Jumpman*, *Miner 2049er*, *Boulder Dash*).
* **What about the IBM PC XT?**
  * `pico-286` without PSRAM gives **200 KB** of conventional RAM on an RP2350 (its README's "virtual memory" figure), not the 384 KB the first draft claimed: the emulator's own code, CGA memory and buffers take the rest of the 520 KB.  That boots DOS and runs small 1981-84 titles only.
  * With the **8 MB QSPI PSRAM** (`APS6404L-3SQR`; `pico-286` auto-detects it) the full **640 KB + EMS + AdLib** configuration is available.  `pico-286` is built for Murmulator boards; the Zero needs its HDMI base pin (6 on Murmulator) and PSRAM pins (18-21) remapped.

### C. Commodore 64 & TI-99/4A — How Core 1 Conquers Sprites
* **TI-99/4A (`TMS9918A`):**
  * The TMS9918A VDP has its own isolated **16 KB VRAM**. On each active scanline (`0..191`), Core 1 draws the Graphics I or Graphics II tile backdrop and border from the VDP registers, walks the 32-entry Sprite Attribute Table, draws the first **4 sprites on the line** (8x8 or 16x16, magnified or not), sets the fifth-sprite status flag and index when more line up, and sets the coincidence bit (`0x20`).  Text and Multicolor modes are not implemented.
* **Commodore 64 (`VIC-II` + `SID`):**
  * On every 63-cycle PAL (or 65-cycle NTSC) scanline (`504 pixels` / `63.5 µs`), Core 0 and Core 1 synchronize via a shared scanline counter. On "badlines" (every 8th scanline where `YSCROLL == (raster & 7)`), Core 1 signals Core 0 to pause the 6510 for 40 cycles while Core 1 fetches the 40-column character pointers, mixes up to **8 hardware sprites** (with X/Y expansion, multicolor mode, and sprite-to-sprite / sprite-to-background collision interrupts), and synthesizes 1 scanline (1 sample @ 15.7 kHz or 3 samples @ 47.2 kHz) of **MOS 6581/8580 SID resonant filter audio** using the Cortex-M33 hardware FPU!

---

## 4. Three Wireless Keyboard & Connectivity Options (With vs. Without the Beetle)

Because the bare RP2350-Zero does not have an onboard radio chip, keeping your friend's **Beetle board** inside the AirTag case as a dedicated wireless co-processor is actually the cleanest way to avoid needing an external USB OTG power/host splitter cable:

### Option A (Best Clean Cabling): Keep the Friend's Beetle Inside for Bluetooth + Wi-Fi
Since you already own the Beetle board and know it fits in the AirTag case, stacking it with the RP2350-Zero (using the exact same 4-wire link from [`01_ti994a_emulator`](file:///home/ccd/retro_pc_project/01_ti994a_emulator/PLAN.md)) gives every RP2350 emulator (**Apple //e, Apple //c, PCjr, PC XT, C64, and TI-99/4A**) three huge upgrades while eliminating the external USB OTG adapter:
1. **Single USB-C Power Cable (No OTG Splitter or Dongle Needed):**
   * The Beetle pairs directly with your wireless **Bluetooth (BLE) keyboard and gamepad**.
   * The Beetle outputs **both** `115200` baud UART HID frames (RP2350 `GPIO 9`, UART1) **and** **PS/2 clock & data** (`GPIO 6 & 7`); PS/2-native emulators (`frank-apple`, `frank-c64`, `pico-286`) need their PS/2 pins pointed at 6/7, UART emulators (`pico-994A` with `uart_hid_bridge.cpp`, `standalone_emulator_main.c`) read the frames.
2. **Wi-Fi "Virtual Floppy Drive" (`http://airtag-retro.local`):**
   * Using its Wi-Fi + 2 MB Flash, the Beetle lets you upload `.DSK` (140 KB Apple II floppy), `.D64` / `.PRG` (C64), or `.IMG` (PCjr/XT DOS disk) images from a browser and stream them over UART (`0xAA 0x55 0x02` blocks) straight into the RP2350-Zero's SRAM—no need to open the AirTag case to swap a MicroSD card!
3. **Emulated Wi-Fi Hayes Modem (`ATDT` / Telnet / SSH Bridge):**
   * The UART link on `GPIO 0/1` can also map directly to the emulated **Apple //c Serial Port**, **Apple //e Super Serial Card**, **IBM PCjr/XT `COM1:` (`0x3F8`)**, or **C64 User Port Modem**—letting vintage terminal software inside the emulator dial into Telnet BBSes or your Wi-Fi network!

### Option B (Single-Board, No Extra Soldering): USB-C OTG Splitter + 2.4 GHz / BT Nano-Dongle
* If you build with *only* the RP2350-Zero, plug a tiny **USB-C OTG Power + USB-A splitter adapter** behind the monitor and insert any standard **2.4 GHz wireless keyboard nano-receiver** (or a $5 Bluetooth-to-USB HID receiver dongle).
* The RP2350-Zero's built-in **TinyUSB Host (`tuh_hid`)** reads the dongle as a standard wired USB keyboard.

### Option C (Single-Board + Internal Radio): Solder a $4 Raspberry Pi Radio Module 2 (`RM2`)
* The official **Raspberry Pi RM2 (`CYW43439`)** measures **$16.5 \times 14.5 \times 2.5\text{ mm}$** and wires to 4 GPIO pads on the RP2350-Zero, turning it into an AirTag-sized **`Raspberry Pi Pico 2 W`** with native `BTstack` Bluetooth support.

---

## 5. RP2350-Zero Pinout Inside the AirTag Case (Standalone vs. With Beetle Co-Processor)

| RP2350-Zero Pin | Connected Component | Function |
| :---: | :--- | :--- |
| **USB-C Port** | **5V USB Power Cable** *(or OTG Splitter in Option B)* | Powers RP2350-Zero (and Beetle via `5V` & `GND` pins in Option A) |
| **GPIO 8 & GPIO 9** *(Option A)* | **Beetle `RX` & `TX`** | `UART1` (`115200` baud): BLE keyboard / joystick HID frames from the Beetle (GPIO 0/1 stay free for each emulator's own console UART) |
| **GPIO 6 & GPIO 7** *(Option A)* | **Beetle `GPIO 4` & `GPIO 5`** | **PS/2 clock & data** for `frank-apple` / `frank-c64` (their board_config.h must be edited: stock M2 uses 2/3, which are the SD pins here) |
| **GPIO 2, 3, 4, 5** | **MicroSD card slot** | `SPI0 SCK` (GP2), `MOSI` (GP3), `MISO` (GP4), `CS` (GP5); same as pico-994A HW_CONFIG 2 and 6 |
| **GPIO 20** *(optional)* | **`APS6404L-3SQR` `CS#`** | optional 8 MB QSPI PSRAM (the first draft said GPIO 8, now the UART) |
| **GPIO 12 & 13** | **Micro-HDMI Type D pins 9 & 11** *(270 Ω)* | **HSTX Data 0 + / -** |
| **GPIO 14 & 15** | **Type D pins 12 & 14** *(270 Ω)* | **HSTX Clock + / -** |
| **GPIO 16 & 17** | **Type D pins 3 & 5** *(270 Ω)* | **HSTX Data 2 + / -** |
| **GPIO 18 & 19** | **Type D pins 6 & 8** *(270 Ω)* | **HSTX Data 1 + / -** (Pico DVI Sock order used by pico-examples and the PicoPlus emulators; `common/hstx_dvi.c` can switch to frank-apple's M2 order with `HSTX_DVI_PINOUT_M2`) |
