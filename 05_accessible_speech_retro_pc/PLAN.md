# Project 5: Accessible "Talking" Retro Computer (RP2350 Core 1 + DECtalk / ESPK)

A self-voicing retro computer built on the **Waveshare RP2350-Zero** (dual Cortex-M33 at 150 MHz, 520 KB SRAM, 4 MB QSPI flash), optionally paired with a Beetle ESP32-C6/C3 for Bluetooth keyboard and Wi-Fi. Core 0 emulates the vintage machine (Z80 CP/M 2.2, 65C02 Apple //e, or 8088 PC XT); Core 1 acts as a dedicated speech coprocessor that appears to the emulated machine as a serial **RC Systems DoubleTalk LT**, synthesizes speech using **DECtalk 4.x** (ARM7/Epson embedded configuration) or **ESPK** (eSpeak-NG English front end + integer Klatt), and runs an **out-of-band hypervisor screen reader** that reads the emulated video RAM.

It builds on three sibling projects:
- [`/home/ccd/vintage-pc-speech`](file:///home/ccd/vintage-pc-speech/README.md) and its working tree [`/home/ccd/accessible_os/freedos`](file:///home/ccd/accessible_os/freedos/src/dectalk/NOTES.md): the DOS ports of DECtalk (`dtalkd`), ESPK (`espkd`), and SBTALK, and the DoubleTalk protocol parser verified against Provox 7 under QEMU.
- [`/home/ccd/rust-tdsr`](file:///home/ccd/rust-tdsr/CLAUDE.md): review cursor, symbol dictionary, key-echo detection, and TUI screen diffing.
- [`/home/ccd/accessible_os`](file:///home/ccd/accessible_os/README.md): the requirement that a blind user hears the machine within the first second of boot, every time.

**Build & test status:** The Core 1 software is implemented, unit-tested on the host with the real speech engines (105 checks), verified bit-identically on a QEMU Cortex-M33 model, and linked into a Pico SDK firmware image (`talking_retro.uf2`) for the RP2350-Zero. Testing on physical hardware and HDMI audio data-island encoding remain open.

---

## 1. Out-of-Band Speech and Screen Reading on Dual-Core RP2350

In the 1980s, a blind user needed an external hardware synthesizer (DECtalk DTC01, DoubleTalk, Echo II) because an 8-bit CPU could not run formant synthesis alongside an operating system, and screen readers competed with applications for RAM and interrupt vectors: CP/M had no universal screen reader, Apple II *Textalker* had to be patched onto each disk, and DOS TSRs such as Provox 7 consumed conventional memory.

On the RP2350, the second Cortex-M33 core handles speech and screen reading out of band:
- **Core 0** runs the emulated CPU with its full original RAM and timing.
- **Core 1** runs DECtalk or ESPK at native 32-bit speed, parses the DoubleTalk command set spoken by existing DOS screen readers, and monitors the emulated VRAM in shared SRAM. The emulated machine loses **no RAM and no CPU cycles**.

Measured on QEMU's Cortex-M33 model (Section 6): DECtalk uses 3.5% of one 150 MHz core per second of speech, and ESPK uses 1.8%, at one instruction per cycle.

---

## 2. Audio Outputs and Startup Sequence

### Two Outputs from One PCM Ring
Core 1 fills an 8192-sample 16-bit ring buffer (743 ms at 11025 Hz):

| Output | Pins | Status |
| :--- | :--- | :--- |
| **3.5 mm headphone, PWM** | GPIO 20 (L), GPIO 21 (R), PWM slice at 73 kHz with 11-bit resolution, RC filter 220 Ω + 100 nF | **Implemented** in `pico_firmware/main.c`: an 11025 Hz repeating timer on Core 1's alarm pool pops one sample per tick. Works without a monitor connected. |
| **HDMI L-PCM data islands** | GPIO 12–19 (HSTX) | **Not implemented.** DVI video scan-out is in `common/hstx_dvi.c`; HDMI data-island packet encoding (IEC 60958 sub-frames and audio clock regeneration) is still to be added. The audio consumer API (`audio_output_pop_block`) matches a DMA refill callback. |

### Startup Sequence and Fallbacks
The power-on sequence on Core 1 runs in the following order so the user gets immediate audible feedback:

1. **Chime** (two tones, 140 ms) rendered directly into the PCM ring before any speech engine, dictionary, or memory allocation is touched. If the chime does not play, the hardware audio path is at fault rather than the synthesizer.
2. **DECtalk init and spoken boot message** (`"DECtalk ready. Talking CP M. Alt H for help."`).
3. **DECtalk warm-up** (`dtalkd`'s two warm-up sentences with audio output discarded) runs immediately after the boot message is queued into the PCM ring, while the last 743 ms of the boot message are still playing. Measured on QEMU, it performs zero heap allocations and completes in 413 ms at 150 MHz (or can be disabled with `DT_POST_BOOT_WARMUP=0`).
4. **ESPK** initializes lazily on first use (`Alt+S`), staying off the boot path.

| Step | Instructions | Time at 150 MHz, 1 IPC |
| :--- | ---: | ---: |
| DECtalk `TextToSpeechInit` | 0.02 M | 0.1 ms |
| First full ring (chime + start of speech) | 3.3 M | 22 ms |
| Full boot message (4.3 s of audio) | 22.8 M | 152 ms |
| DECtalk warm-up (runs during boot message playback) | 62.0 M | 413 ms |

**Fallbacks and Watchdog:** If an engine fails to initialize, or renders a clause containing letters/digits and produces zero audio samples, it is marked failed and the clause is re-rendered with the next engine (`DECtalk → ESPK → tone voice`). In the firmware, Core 0 monitors a Core 1 heartbeat counter (updated inside the idle hook) and relaunches Core 1 if it stalls for 5 s; the relaunched core chimes and announces `"Speech restarted"` without interrupting the emulated machine on Core 0.

**Panic Chord (`Alt+Shift+Backspace`):** Flushes speech, resets rate to 200 wpm and volume to default, and announces the cursor position.

---

## 3. Speech Engines on Core 1

Whisper WER figures below use `base.en` on the 24-sentence set from `vintage-pc-speech/src/espeak/results/sentences24.txt`. Object sizes are from `arm-none-eabi-size` at `-O2` for Cortex-M33:

| Engine | Source | WER (`base.en`) | Flash | SRAM Static | Stack | Heap | Core 1 Load @ 150 MHz | Notes |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| **DECtalk 4.x ARM7** (`epsonapi.c`, full US dictionary) | `accessible_os/freedos/src/dectalk` + `github.com/dectalk/dectalk` @ `69ebb459` | **12.4%** | **667 KB** (318 KB code, 20 KB data, 329 KB dict) | 54 KB (34 KB BSS + 20 KB data) | 2.3 KB | 0 | **3.5%** (5.3 M instructions / audio sec, 28× real time) | **Primary voice.** Fonix proprietary source. |
| **ESPK** (eSpeak-NG English + `klatt_fx` Q12) | `vintage-pc-speech/src/espeak`, `src/klatt` | 26–30% at 8 kHz 8-bit; 22–24% at 11 kHz F1–F4 | **514 KB** (136 KB code, 378 KB `ESPK.DAT` in XIP flash) | 79 KB BSS | 47.5 KB | 12 KB | **1.8%** (2.7 M/s, 56×) | **Secondary voice, GPLv3+.** Current output path is 8-bit (`wgout.c`); 16-bit via `kfx_render16` is a planned improvement. |
| Test tone | Built in | – | 1 KB | – | – | – | – | Proves the audio path without an engine. |
| SAM / SBTALK | `vintage-pc-speech/src/sbtalk` | 33% | 28 KB | 8 KB | – | – | <1% | Not integrated. |

### Component Object Sizes (Cortex-M33, `-O2`)

| Component | `text` (bytes) | `data` (bytes) | `bss` (bytes) |
| :--- | ---: | ---: | ---: |
| DECtalk engine code (67 files + glue) | 318,113 | 19,594 | 33,700 |
| DECtalk full US dictionary (`maindict.c`) | 328,763 | 0 | 0 |
| ESPK code + tables | 136,396 | 84 | 78,670 |
| `ESPK.DAT` (XIP flash) | 378,432 | 0 | 0 |
| Core 1 speech core + screen reader | 11,998 | 190 | 33,205 |

### DECtalk Build Details
- **Required defines:** `-DARM7 -DENGLISH_US -DACNA -DARM7_NOSWI -DENGLISH -Di386 -DWIN32_TEST -DBLD_DECTALK_DLL -DCHEESY_DICT_COMPRESSION`, linked with the full US dictionary (`build-gen/maindict.c`, 329 KB). `-DCHEESY_DICT_COMPRESSION` is mandatory so dictionary lookups read entry offsets properly and preserve natural clause intonation.
- **Translation unit isolation:** DECtalk's `include/port.h` defines `#define volatile` to empty on `ARM7`. Any translation unit that includes `epsonapi.h` loses `volatile` qualifiers, so the DECtalk glue lives in its own file ([`engine_dectalk.c`](file:///home/ccd/retro_pc_project/05_accessible_speech_retro_pc/speech_core1/engine_dectalk.c)) and includes `doubletalk_core1.h` before `epsonapi.h`.
- **Sample callback semantics:** `short *cb(short *buf, long code)` passes `code == 0` for an audio block of 71 samples (11025 Hz) or 51 samples (8000 Hz) and `code == 3` for an index mark with no audio. Returning `NULL` from the callback sets `halting` and causes the next `TextToSpeechStart` to hang in `ls_util_next_item_new`; on a flush, the callback continues returning `buf` while discarding samples.
- **DoubleTalk parameter mapping:** Speed `0..9` → `[:ra 100+20·s]` (`5` = 200 wpm); pitch `0..99` → `[:dv ap 60+1.25·p]` (`50` = 122 Hz); voice `0..7` → `[:np] [:nb] [:nh] [:nf] [:nd] [:nk] [:nu] [:nr]`; volume `0..9` → `{0, 20, 35, 50, 65, 80, 85, 90, 95, 100}%`.

---

## 4. Core 1 Software Architecture

```
speech_core1/doubletalk_core1.[ch]         Virtual DoubleTalk LT: FIFOs, parser, flush protocol, PCM ring, engine dispatch
speech_core1/engine_dectalk.c              DECtalk ARM7 glue (isolated translation unit)
speech_core1/engine_espk.c                 ESPK glue; espk_data_mem.c serves ESPK.DAT from flash in place
speech_core1/compat/dirent.h               Bare-metal stand-in for unused directory scan in ESPK
screen_reader/tdsr_hypervisor_reader.[ch]  VRAM adapters (VT100 cells, Apple //e $0400, CGA $B8000), diff engine, review keys
host_test/                                 sim_core1: 105 checks against the real engines, writes WAVs
arm_qemu/                                  Runs the same code on QEMU Cortex-M33 (mps2-an505): sizes, instruction counts, WAVs
pico_firmware/                             Pico SDK project for the RP2350-Zero (Core 0 stub + Core 1 + PWM audio) -> UF2
cpm_disk_A/speak.asm, SPEAK.COM            CP/M 2.2 speech utility using BDOS 224
build_accessible_retro.sh                  Build and test driver script
```

### Inter-Core Threading Contract
- **Core 0 → Core 1 FIFOs:** Two single-producer/single-consumer (SPSC) rings in shared SRAM: a 2 KB byte FIFO for the synthesizer port and a 64-entry key-event FIFO. Core 0 never blocks; each core only updates its own ring index.
- **Flush (`^X`) Protocol:** An atomic counter `g_dt_flush_req` plus a snapshot of the text FIFO write position. The audio consumer clears the PCM ring on its next sample tick (≤ 91 µs); the parser discards bytes queued prior to the snapshot; an active synthesis call checks `synth_flush_pending()` and aborts; a new utterance waits for the consumer's flush acknowledgement before pushing samples.
- **Core 1 Main Loop:** `core1_speech_poll()` parses, renders, and waits when the PCM ring is full; during that wait, the **idle hook** runs the screen reader (captures VRAM, processes keys, runs the 30 ms screen diff) and executes `__wfe()`. Screen reader utterances use a dedicated Core-1-only `core1_say` queue.
- **Keyboard Routing:** Core 0 calls `doubletalk_key_event(ascii, mods)` before passing a key to the emulated OS. Review hotkeys (`Alt+...`) are filtered by a pure predicate on Core 0 and executed on Core 1 via the key FIFO; regular keys trigger a speech flush.
- **Clause Boundary Rules:** Utterances end on `CR`, `LF`, `NUL`, after 60 ms of idle input, or at the last space of a full 200-byte buffer (never on `.`, `?`, or `!`, so abbreviations and decimals stay intact). `^A<n>Z` selects the speech engine (`0` = DECtalk, `1` = ESPK, `2` = tone).
- **16550 Register View (`COM2`):** Returns `LSR = 0x60`, `MSR = 0xB0`, `IIR = 0x01`, `INT 14h = 0x60B0`, and ignores divisor-latch writes.

### Memory Budget on the RP2350-Zero (Pico SDK Build)
The RP2350 has 520 KB of internal SRAM (8 banks of 64 KB + 2 banks of 4 KB).

| Component | Flash | SRAM |
| :--- | ---: | ---: |
| Complete `talking_retro.elf` (Pico SDK, Core 1 + screen reader, DECtalk + dict, ESPK + `ESPK.DAT`, 64 KB Core 1 stack) | **1,275 KB** | **220 KB** (`.bss` incl. stack) |
| — of which Core 1 speech core + screen reader | 12 KB | 33 KB (16 KB PCM ring, 2 KB FIFO, 9.6 KB screen grids, say queue) |
| **Remaining for Core 0 (emulated machine)** | **~2.7 MB** | **~300 KB** |

300 KB of free SRAM fits CP/M 2.2 (64 KB TPA + 128 KB RAM disk), an Apple //e (128 KB) plus a 140 KB floppy image in RAM, or a 256 KB PC. A 384–640 KB PC XT requires either the optional 8 MB QSPI PSRAM chip or building without ESPK (saving 79 KB BSS and 48 KB of stack). Note that the Core 1 stack is explicitly allocated at 64 KB because the Pico SDK default is 2 KB and ESPK's translator uses 47.5 KB.

---

## 5. Three Accessible Personalities

### Personality 1: Talking CP/M 2.2
Pairs CP/M 2.2 with the Core 1 screen reader monitoring the 80×30 VT100 `TermCell {ch, fg, bg}` grid (`tdsr_capture_termcells`). Uses zero bytes of Z80 RAM.
- **Console Mode:** Echoes typed keys once on press and suppresses the subsequent screen echo within 400 ms; reads new output lines appearing between the old and new cursor rows; compensates for scrolling so only the newly scrolled bottom line is read; silences full-screen repaints and snaps the review cursor to the active cursor.
- **TUI Mode:** Reads newly highlighted spans (cells whose background differs from the row's dominant background, such as in WordStar menus or SuperCalc cells); arrow up/down reads the new line, left/right reads the character, and backspace speaks the deleted character.
- **Review Keys (`rust-tdsr` layout):** `Alt+U/I/O` lines (`Alt+I` twice reads all punctuation; with `Shift` jumps to top/bottom), `Alt+J/K/L` words (`Alt+K` twice spells the word), `Alt+M/,/.` characters (`Alt+,` twice speaks NATO phonetics; with `Shift` jumps to start/end of line), `Alt+C` announces cursor position, `Alt+R` cycles punctuation level, `Alt+S` switches voice engine, `Alt+X` toggles key echo, `Alt+=/−` adjusts speech rate, `Alt+H` speaks help. Repeated symbols are condensed (e.g., `"11 equals"`).
- **`SPEAK.COM` (`A>SPEAK [/Sn] [/Pnn] [/Vn] [/On] [/X] text`, 393 bytes):** Formats options as DoubleTalk commands and invokes host **BDOS 224**, which streams the NUL-terminated string at `DE` to the synthesizer FIFO. Verified under `z80pack` `cpmsim` and under RunCPM 6.9 using the BDOS 224 patch in `02_beetle_terminal_os/os_cpm22_runcpm/runcpm_patch/`.

### Personality 2: Talking IBM PC XT / PCjr (Provox 7 on Virtual DoubleTalk)
Unmodified `PROVOX7.EXE` (GPL, 23 KB) configured with `PV7 LITETALK COM2` communicates with the virtual 16550 UART at `COM2`. The Core 1 hypervisor screen reader monitors CGA text RAM at `B8000` during BIOS POST and on non-Provox disks, and automatically yields (`com2_provox_active`) as soon as Provox writes to `COM2`.

### Personality 3: Talking Apple //e and //c
Decodes the interleaved 40-column text page (`$0400–$07FF`) and 80-column auxiliary memory: `$00–$3F` inverse, `$40–$7F` flashing mapped to the inverse glyph set (so `$60`, a flashing space cursor, reads as space rather than grave accent), or with `ALTCHARSET` enabled maps `$40–$5F` as MouseText decoration and `$60–$7F` as inverse lowercase, and `$80–$FF` as normal ASCII. Any unpatched `.DSK` image speaks via the hypervisor reader, while Slot 4 (`$C0C0`) also forwards Echo II / DoubleTalk writes for classic blind Apple II software.

---

## 6. Verification Summary

All verification suites can be run via `./build_accessible_retro.sh [--arm] [--pico] [--cpm] [--whisper]`:

| Suite | Description | Result |
| :--- | :--- | :--- |
| Host (`host_test/sim_core1`) | Core 1 code linked with DECtalk and ESPK; simulated 11025 Hz audio consumer | **105 checks pass**: boot chime/message, post-boot warm-up, engine fallback, DoubleTalk parser, idle clause, 200-byte split, mid-utterance `^X` flush, say-queue ordering, 16550 registers, FIFO overrun, Apple //e & CGA decoding, CP/M console diffing, TUI highlight tracking, review keys, engine switch, Provox deferral |
| Whisper `base.en` | Intelligibility check on generated WAVs | DECtalk: `"The quick round fox jumps over the lazy dog and then it runs away into the forest."` (1/17 words). ESPK: `"The quick round box jumps…"` (2/17 words). Flush WAV: `"This is…"` |
| QEMU Cortex-M33 (`arm_qemu`) | Same code on `mps2-an505` with `-icount shift=0` instruction counting | Produces WAVs **byte-identical** to the host build; measures cycle counts and stack high-water (2,344 B DECtalk; 47,528 B ESPK) |
| Pico SDK (`pico_firmware`) | `PICO_BOARD=waveshare_rp2350_zero`, both engines, PWM audio, 64 KB Core 1 stack | Builds cleanly: 1,275 KB text, 220 KB BSS, `talking_retro.uf2` (2.55 MB) |
| `z80pack` `cpmsim` & RunCPM 6.9 | `SPEAK.COM` under CP/M 2.2 | Verified usage output, option flags, and BDOS 224 byte stream (`^A7S^A60PHELLO WORLD`) |

---

## 7. Open Work

1. **Hardware testing:** Flash `talking_retro.uf2` to a Waveshare RP2350-Zero, test PWM headphone audio on GPIO 20/21 through the 220 Ω + 100 nF filter, and measure XIP flash cache performance via `g_dt_stats`.
2. **HDMI audio:** Add HDMI L-PCM data-island packet encoding (IEC 60958 sub-frames and audio clock regeneration packets) to `common/hstx_dvi.c`.
3. **Core 0 emulator integration:** Connect Core 0 CPU emulators (`BDOS 224`, `OUT 2F8h`, `STA $C0C0`) to `doubletalk_port_write_byte()`, route keyboard input through `doubletalk_key_event()`, and call `tdsr_capture_*()` from the Core 1 idle hook.
4. **ESPK 16-bit audio path:** Switch ESPK from 8-bit `wgout.c` to 16-bit `kfx_render16` and match output gain with DECtalk.
5. **Additional screen reader features:** Box-drawing window detection, scrollback review, and optional 8 MB PSRAM support for a 640 KB PC XT configuration.
