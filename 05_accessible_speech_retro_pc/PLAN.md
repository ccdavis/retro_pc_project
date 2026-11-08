# Project 5: Accessible "Talking" Retro Computer (RP2350 Core 1 + DECtalk / ESPK)

A self-voicing retro computer in the AirTag-sized **Waveshare RP2350-Zero**
(dual Cortex-M33 at 150 MHz, 520 KB SRAM, 4 MB QSPI flash), optionally with the
Beetle ESP32 for Bluetooth keyboard and Wi-Fi.  Core 0 emulates the vintage
machine (Z80 CP/M 2.2, 65C02 Apple //e, 8088 PC XT); Core 1 is a dedicated speech
coprocessor that looks to the emulated machine like a serial **RC Systems
DoubleTalk LT**, renders speech with **DECtalk 4.x** (ARM7/Epson embedded
configuration) or **ESPK** (eSpeak-NG English front end + integer Klatt), and runs
an **out-of-band hypervisor screen reader** that reads the emulated video RAM.

It builds on three sibling projects:
- [`/home/ccd/vintage-pc-speech`](file:///home/ccd/vintage-pc-speech/README.md) and its
  working tree [`/home/ccd/accessible_os/freedos`](file:///home/ccd/accessible_os/freedos/src/dectalk/NOTES.md):
  the DOS ports of DECtalk (`dtalkd`), ESPK (`espkd`) and SBTALK, and the DoubleTalk
  protocol parser proven against Provox 7 under QEMU.
- [`/home/ccd/rust-tdsr`](file:///home/ccd/rust-tdsr/CLAUDE.md): review cursor, symbol
  dictionary, key-echo detection and TUI screen diffing.
- [`/home/ccd/accessible_os`](file:///home/ccd/accessible_os/README.md): the requirement
  that a blind user hears the machine within the first second, every time.

**Status (2026-11-07):** the Core 1 software is written, unit-tested on the host
with the real engines (105 checks), runs bit-identically on a QEMU Cortex-M33, and
links into a Pico SDK firmware image for the RP2350-Zero.  Nothing has run on
real hardware yet, and HDMI audio is not implemented.  See `REVIEW.md` for what
was wrong in the first draft and how each number below was measured.

---

## 1. Why a dual-core RP2350 changes retro accessibility

In the 1980s a blind user needed a hardware synthesizer that cost more than the
computer (DECtalk DTC01 $4,000; DoubleTalk $299; Echo II $199) because an 8-bit
CPU could not run formant synthesis beside an operating system, and screen
readers competed with applications for RAM and interrupt hooks: CP/M never had a
universal screen reader, Apple II *Textalker* had to be patched onto each disk,
and DOS TSRs such as Provox 7 ate conventional memory.

On the RP2350 the second Cortex-M33 core does all of that out of band:
- **Core 0** runs the emulated CPU with its full original RAM and timing.
- **Core 1** runs DECtalk or ESPK at native 32-bit speed, parses the DoubleTalk
  command set the DOS screen readers already speak, and watches the emulated
  VRAM in shared SRAM.  The emulated machine loses **no RAM and no cycles**.

Measured on QEMU's Cortex-M33 model (section 6): DECtalk needs 3.5 % of one
150 MHz core per second of speech, ESPK 1.8 %, at one instruction per cycle.

---

## 2. Foolproof "plug-and-talk" audio

### Two outputs from one PCM ring
Core 1 fills one 8192-sample 16-bit ring (743 ms at 11025 Hz).  Two consumers
are planned; one exists:

| Output | Pins | State |
| :-- | :-- | :-- |
| **3.5 mm headphone, PWM** | GPIO 20 (L), GPIO 21 (R), PWM slice at 73 kHz with 11-bit resolution, RC filter 220 Ω + 100 nF | **Implemented** in `pico_firmware/main.c`: an 11025 Hz repeating timer on Core 1's own alarm pool pops one sample per tick.  Works with no monitor connected. |
| **HDMI L-PCM data islands** | GPIO 12–19 (HSTX) | **Not implemented.**  The DVI scan-out exists in `common/hstx_dvi.c`; the data-island packet encoding (audio clock regeneration, info frames) does not.  Plan: interleave 48 kHz stereo IEC 60958 packets upsampled from the 11025 Hz ring.  The audio consumer API (`audio_output_pop_block`) is already the shape a DMA refill needs. |

### Quick, reliable start-up (measured)
The goal is that a blind user hears the machine quickly and *every time*; the
exact figure matters less than reliability (anything under about a second is
fine).  The power-on sequence on Core 1, in order:

1. **Chime** (two tones, 140 ms) rendered straight into the PCM ring with no
   engine, dictionary or allocation involved.  If the chime is not heard, the
   audio path or the board is dead, not the synthesizer.
2. **DECtalk init and the spoken message** ("DECtalk ready. Talking CP M. Alt H
   for help.").
3. **DECtalk warm-up** (dtalkd's two sentences with the output discarded) runs
   as soon as the message has been handed to the ring, i.e. while its last
   743 ms are still playing.  It is the DOS resident's proven start-up order,
   kept as belt and braces: it exercises numbers, abbreviations and punctuation
   once before the user does.  Measured on QEMU it allocates nothing (heap 0
   bytes before and after; the `malloc` calls in the compiled files are on paths
   the ARM7 configuration does not take), so it is not load-bearing, and it costs
   nothing audible as long as it finishes inside the 743 ms the ring still
   holds (413 ms on QEMU; if XIP flash makes it slower than that on the board,
   the only effect is a gap before the *next* utterance, once).  Bytes from the
   emulated machine that arrive during it wait in the FIFO.
   `DT_POST_BOOT_WARMUP=0` disables it.
4. **ESPK** initialises, with its own warm-up, on first use (Alt+S), never on
   the boot path.

| Step | Instructions | Time at 150 MHz, 1 IPC |
| :-- | --: | --: |
| DECtalk `TextToSpeechInit` | 0.02 M | 0.1 ms |
| First full ring (chime + start of speech) | 3.3 M | 22 ms |
| Whole boot message (4.3 s of audio) | 22.8 M | 152 ms |
| DECtalk warm-up (after the message, during playback) | 62 M | 413 ms |

So the chime starts a few milliseconds after Core 1 launches and speech about
20 ms after that, before any disk image is mounted or Bluetooth connects; the
warm-up is finished long before the message ends.  Real silicon runs from QSPI
flash through a 16 KB XIP cache; expect a few times slower, still far inside a
second.

**Fallbacks.**  An engine that fails to initialise, or renders a clause with
letters or digits and produces no audio, is marked failed and the clause is
re-rendered with the next engine: DECtalk → ESPK → tone voice.  In the firmware
Core 0 watches a Core 1 heartbeat (bumped in the idle hook, so it keeps beating
even while an utterance blocks on the full ring) and relaunches Core 1 if it
stops for 5 s; the relaunched core chimes and says "Speech restarted" while the
emulated machine on Core 0 carries on untouched.

Spoken events: Bluetooth keyboard connected / no keyboard after 10 s (Core 0
sends them through the same port); **Alt+Shift+Backspace** is the panic chord:
speech flushed, rate 200 wpm, pitch and volume reset, cursor position announced.

---

## 3. Speech engines on Core 1, measured

Whisper WER figures are `base.en` on the 24-sentence set from
`vintage-pc-speech/src/espeak/results/sentences24.txt` (the first draft of this
plan quoted ESPK's `small.en` number next to DECtalk's `base.en` number, which
made ESPK look better than DECtalk; at these sample rates it is about twice as
error-prone).  Sizes are `arm-none-eabi-size` of the `-O2` Cortex-M33 objects.

| Engine | Source | WER (base.en) | Flash | SRAM static | Stack | Heap | Core 1 load at 150 MHz | Verdict |
| :-- | :-- | --: | --: | --: | --: | --: | --: | :-- |
| **DECtalk 4.x ARM7** (`epsonapi.c`, full US dictionary) | `accessible_os/freedos/src/dectalk` + github.com/dectalk/dectalk @69ebb459 | **12.4 %** | **667 KB** (318 KB code, 20 KB data, 329 KB dictionary) | 54 KB (34 KB bss + 20 KB data) | 2.3 KB | 0 | **3.5 %** (5.3 M instructions per audio second, 28× real time) | **Primary voice.**  Licence: Fonix proprietary, source circulating since 2015, rights holder defunct. |
| **ESPK** (eSpeak-NG English + `klatt_fx` Q12) | `vintage-pc-speech/src/espeak`, `src/klatt` | 26–30 % at 8 kHz 8-bit; 22–24 % at 11 kHz F1–F4 | **514 KB** (136 KB code, 378 KB `ESPK.DAT` used in place) | 79 KB bss | 47.5 KB | 12 KB | **1.8 %** (2.7 M/s, 56×) | **Second voice, GPLv3+.**  Output path is 8-bit (`wgout.c`); a 16-bit path via `kfx_render16` is an easy improvement. |
| Test tone | built in | – | 1 KB | – | – | – | – | Always present: proves the audio path without any engine. |
| SAM / SBTALK | `vintage-pc-speech/src/sbtalk` | 33 % | 28 KB | 8 KB | – | – | <1 % | **Not integrated.**  Needs the `ring_write5` renderer hook; licence unclear.  Low value next to ESPK. |

### DECtalk build facts that matter
- Defines: `-DARM7 -DENGLISH_US -DACNA -DARM7_NOSWI -DENGLISH -Di386 -DWIN32_TEST -DBLD_DECTALK_DLL -DCHEESY_DICT_COMPRESSION`
  plus the full dictionary compiled from `dic/Dic_us.txt` (`build-gen/maindict.c`).
  **`CHEESY_DICT_COMPRESSION` is not optional**: without it every lookup misreads
  the entries and every word gets its own pitch accent (freedos `NOTES.md`,
  "Intonation fix").  The first draft budgeted the 4,809-word subset (160 KB);
  the real dictionary is 329 KB.
- `include/port.h` does `#define volatile` to nothing for ARM7.  Any translation
  unit that includes `epsonapi.h` loses `volatile`, which silently breaks
  lock-free ring buffers.  Hence the engine glue lives in its own file.
- The sample callback is `short *cb(short *buf, long code)`: `code` 0 means one
  block of 71 (11025 Hz) or 51 (8000 Hz) samples is in `buf`, `code` 3 is an
  index mark with no audio.  It is **not** a sample count; the first draft looped
  over it and would have produced silence.  The returned pointer becomes the
  next output buffer; returning `NULL` sets `halting`, re-initialises the engine
  and hangs the next `TextToSpeechStart` in `ls_util_next_item_new`.  On a flush
  the callback keeps returning the buffer and discards (dtalkd's fix).
- DoubleTalk mapping as in `dtalkd/engine_dectalk.c`: speed 0..9 → `[:ra 100+20·s]`
  (5 = 200 wpm); pitch 0..99 → `[:dv ap 60+1.25·p]` (50 = 122 Hz); voice 0..7 →
  `[:np] [:nb] [:nh] [:nf] [:nd] [:nk] [:nu] [:nr]`; volume 0..9 through the
  dtalkd percentage table {0,20,35,50,65,80,85,90,95,100}.

---

## 4. The Core 1 software (what is in this directory)

```
speech_core1/doubletalk_core1.[ch]   Virtual DoubleTalk LT: FIFOs, parser, flush protocol, PCM ring, engine dispatch
speech_core1/engine_dectalk.c        DECtalk ARM7 glue (own translation unit, see above)
speech_core1/engine_espk.c           ESPK glue; espk_data_mem.c serves ESPK.DAT from flash in place
speech_core1/compat/dirent.h         bare-metal stand-in for the directory scan ESPK never calls
screen_reader/tdsr_hypervisor_reader.[ch]  VRAM adapters (VT100 cells, Apple //e $0400, CGA $B8000), diff engine, review keys
host_test/                           sim_core1: 98 checks against the real engines, writes WAVs
arm_qemu/                            the same code on QEMU's Cortex-M33 (mps2-an505): sizes, instruction counts, WAVs
pico_firmware/                       Pico SDK project for the RP2350-Zero (Core 0 stand-in + Core 1 + PWM audio) -> UF2
cpm_disk_A/speak.asm, SPEAK.COM      CP/M 2.2 utility for BDOS 224
build_accessible_retro.sh            runs all of the above
```

### Threading contract
- **Core 0 → Core 1**: two single-producer/single-consumer rings in shared SRAM,
  a 2 KB byte FIFO for the synthesizer port and a 64-entry key-event FIFO.  Core 0
  never blocks; a full FIFO drops and counts.  No core ever writes another
  core's ring index (the first draft reset the consumer index from the producer
  on `^X`).
- **Flush (`^X`)**: an atomic counter `g_dt_flush_req` that either core bumps,
  plus a snapshot of the text FIFO write position.  The audio consumer drops the
  ring on the next sample tick (≤ 91 µs); the parser discards the bytes queued
  before the snapshot; a running utterance sees `synth_flush_pending()` and
  stops; a new utterance waits for the consumer's acknowledgement before it
  pushes samples so the drop can never swallow it.  Measured: 400 ms played,
  0 samples after the flush, next utterance plays.
- **Core 1 main loop**: `core1_speech_poll()` parses, renders and blocks while the
  ring is full; during that wait the **idle hook** runs the screen reader
  (capture VRAM, pop keys, 30 ms diff) and `__wfe()`.  The screen reader queues
  its own utterances (`core1_say`) rather than parsing them as DoubleTalk bytes.
- **Keys**: Core 0 calls `doubletalk_key_event(ascii, mods)` before the emulated
  OS sees a key.  The routing decision (is it an Alt review key?) is a pure
  function answered on Core 0; the action runs on Core 1.  Any ordinary key
  flushes speech first.
- **Clause rules** (from `dtalkd/synth.c`): an utterance ends at CR, LF, NUL,
  after 60 ms of idle input, or at the last space of a full 200-byte buffer,
  never at `.`, `?` or `!` (DECtalk handles "3.5" and "Mr. Smith" itself).
  `^A^A` restarts a command; `^X` inside a command is still a flush; unknown
  command letters are accepted and ignored.  Extension: `^A<n>Z` selects the
  engine (0 DECtalk, 1 ESPK, 2 tone).
- **16550 view** for PC emulators: LSR 0x60, MSR 0xB0, IIR 0x01, divisor latch
  writes do not become text; INT 14h status 0x60B0.

### Memory budget on the RP2350-Zero (measured, Pico SDK build)
520 KB SRAM is 8 banks of 64 KB plus 2 of 4 KB (the first draft said 10 × 52 KB).

| | Flash | SRAM |
| :-- | --: | --: |
| Whole `talking_retro.elf` (SDK, Core 1 core + reader, DECtalk + dictionary, ESPK + ESPK.DAT, 64 KB Core 1 stack) | **1,275 KB** | **220 KB** (.bss incl. stack) |
| of which Core 1 speech core + screen reader | 12 KB | 33 KB (16 KB PCM ring, 2 KB FIFO, 9.6 KB screen grids, say queue) |
| **Left for the emulated machine** | ~2.7 MB for disk images | **~300 KB** |

300 KB holds CP/M (64 KB TPA + 128 KB RAM disk) or an Apple //e (128 KB) with a
140 KB disk image in RAM, or a 256 KB PC.  A 384–640 KB PC XT needs the optional
8 MB QSPI PSRAM (APS6404L) on the underside pads, or dropping ESPK (79 KB bss,
48 KB of the stack).  The Core 1 stack must be allocated explicitly: the SDK's
default is 2 KB and ESPK's translator uses 47.5 KB (DECtalk 2.3 KB).

---

## 5. The three accessible personalities

### Personality 1: Talking CP/M 2.2 (the system that never existed)
The 192 KB CP/M from project 2 (63.5 KB TPA, 128 KB RAM disk, `SSH.COM`) with
the Core 1 reader watching the VT100 cell grid (`TermCell {ch, fg, bg}` of
`rp2350_hdmi_vt100_card.c`, 80×30) and the cursor.  Zero bytes of Z80 RAM.
- **Console behaviour** (tested): typed keys are echoed once when pressed and the
  screen echo is recognised and not read again; output lines that appear between
  the old and new cursor row are read in order ("A: SPEAK COM : WS COM…", then
  "A greater"); a scroll is undone before diffing so only the new bottom line is
  read; a full-screen repaint is silent and snaps the review cursor.
- **TUI behaviour** (tested): a newly highlighted span (distinct background on
  its row: WordStar menu, SuperCalc cell, Turbo Pascal bar) is read and wins
  over status-line noise; cursor up/down reads the new line, left/right the
  character; backspace says the character being deleted.
- **Review keys** (rust-tdsr layout): Alt+U/I/O lines (Alt+I twice: all
  punctuation; Shift: top/bottom), Alt+J/K/L words (Alt+K twice spells),
  Alt+M/,/. characters (Alt+, twice: NATO; Shift: start/end of line), Alt+C cursor
  position, Alt+R punctuation level, Alt+S next voice, Alt+X key echo, Alt+=/−
  speed, Alt+H help.  Runs of symbols read as "11 equals".
- **`SPEAK.COM`** (`A>SPEAK [/Sn] [/Pnn] [/Vn] [/On] [/X] text`, 393 bytes):
  turns the options into DoubleTalk commands and calls host **BDOS 224**, which
  streams the NUL-terminated string at DE to the synthesizer port byte for byte.
  Verified under z80pack `cpmsim` with a test build that prints the exact byte
  stream (`^A7S^A60PHELLO WORLD`) and under RunCPM 6.9 with the BDOS 224 hook from
  `02_beetle_terminal_os/os_cpm22_runcpm/runcpm_patch/` receiving the same bytes.  Note
  CP/M's CCP upper-cases the command tail.  The program exits through the warm-boot
  vector; a `RET` on its private stack looped on RunCPM.  `LST:`/`PUN:` can be routed to the same port by the BIOS.

### Personality 2: Talking IBM PC XT / PCjr (Provox 7 on a virtual DoubleTalk)
`PROVOX7.EXE` (GPL, 23 KB) configured `PV7 LITETALK COM2` talks to the 16550
view above; every byte lands in the Core 1 FIFO, status reads never wait.  The
hypervisor reader watches CGA text at `B8000` (character/attribute pairs; a cell
is highlighted when its background differs from the row's dominant background;
the blink bit alone is not a highlight) during POST and on disks without Provox,
and defers (`com2_provox_active`) once Provox owns the port.

### Personality 3: Talking Apple //e and //c (universal Textalker)
The reader decodes the interleaved 40-column text page (`$0400–$07FF`, rows in
three groups of eight) and 80-column mode (even columns from auxiliary RAM).
Character set handling was wrong in the first draft and is now tested: `$00–$3F`
inverse, `$40–$7F` flashing **mapped to the glyphs of the inverse set** (a
flashing cursor is `$60`, a space, not a grave accent), with `ALTCHARSET` the
same range is MouseText (`$40–$5F`, treated as decoration) and inverse lower case
(`$60–$7F`), `$80–$FF` normal.  Every unpatched `.dsk` speaks; a Slot 4 Echo
II/DoubleTalk at `$C0C0` forwards to the same port for classic blind software.

---

## 6. How it was verified

| Level | What | Result |
| :-- | :-- | :-- |
| Host (`host_test/sim_core1`) | Core 1 code with the real DECtalk and ESPK engines; a simulated audio consumer advances a clock as it pops; every utterance logged | **105 checks pass**: boot chime and message, post-boot warm-up, engine fallback, DoubleTalk parsing, idle clause, 200-byte split, mid-utterance flush (400 ms played, 0 after, resumes), say-queue/flush ordering, 16550 view, FIFO overrun, Apple //e and CGA decoding, CP/M console scenario, highlight tracking, review keys, engine switch, Provox deferral |
| Whisper `base.en` on the WAVs | intelligibility | DECtalk: "The quick **round** fox jumps over the lazy dog and then it runs away into the forest." (1/17 words). ESPK: "The quick round **box** jumps…" (2/17). Flush WAV: "This is…" |
| QEMU Cortex-M33 (`arm_qemu`) | same code, `-icount` instruction counting, semihosting | sizes and costs in sections 2–4; flush and console scenario pass; **WAVs byte-identical to the host build** |
| Pico SDK (`pico_firmware`) | `PICO_BOARD=waveshare_rp2350_zero`, both engines, PWM audio, 64 KB Core 1 stack | links: 1,275 KB text, 220 KB bss, `talking_retro.uf2` |
| z80pack `cpmsim`, RunCPM 6.9 | `SPEAK.COM` on CP/M 2.2 | usage prints, options become the right bytes, BDOS 224 reaches the host hook |
| Real RP2350-Zero | | **not yet** |

QEMU has no RP2350 model; `mps2-an505` has the same Cortex-M33 core (FPU and
DSP), no XIP cache, no bus contention from Core 0, and a 20 MHz SysTick that
under `-icount shift=0` counts instructions exactly (calibrated: 50 per tick).
Treat its timings as a floor.

---

## 7. Open items, in order
1. **Flash it.** Build `pico_firmware`, hold BOOT, copy `talking_retro.uf2`, listen on
   GPIO 20/21 through the RC filter; the UART console (GPIO 0/1, 115200) accepts
   typed text and `ESC`+letter as Alt+letter.  Measure the real boot latency and
   the XIP slowdown with `g_dt_stats`.
2. **HDMI audio**: the HSTX DVI scan-out now exists (`common/hstx_dvi.c`, used by the project 2 HDMI card and the project 3 skeleton; compile-checked, not yet seen on a monitor). What remains is the data-island packet encoding for audio (the HSTX command list would need RAW islands with the IEC 60958 sub-frames and the audio clock regeneration packets, as the PicoPlus `hstx_data_island_queue.c` does).
3. **Real emulator cores on Core 0**: route BDOS 224 / `OUT 2F8h` / `STA $C0C0`
   to `doubletalk_port_write_byte`, keys through `doubletalk_key_event`, and call
   the matching `tdsr_capture_*` from the idle hook.
4. **ESPK 16-bit output** and level matching (it is ~10 dB quieter than DECtalk
   even at `level_db = 6`); make "E speak" pronounceable in the boot message
   (done) and re-check with Whisper.
5. **Screen reader gaps vs rust-tdsr**: box-frame/window detection, "repeat
   highlight" (Alt+H is help here), scrollback review, per-row volatility is
   minimal.
6. **PSRAM** for the 640 KB XT personality.
7. **Licensing**: the firmware image contains Fonix DECtalk code (proprietary,
   circulating) and GPLv3+ ESPK; distribute as the vintage-pc-speech releases do,
   or build ESPK-only.
