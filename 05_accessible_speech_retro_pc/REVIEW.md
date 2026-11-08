# Review of the first draft (2026-11-07) and what changed

The first draft of this directory was two header-only files, a 128-byte
`SPEAK.COM` and a build script whose "tests" never exercised a speech engine.
This review lists what was wrong, what was done about it, and how the new
numbers were obtained.  Host checks: 105 pass. Everything here is reproducible with
`./build_accessible_retro.sh [--arm] [--pico] [--cpm] [--whisper]`.

## Defects found

### Would not have worked at all
| # | Where | Problem | Fix |
| :-- | :-- | :-- | :-- |
| 1 | `rp2350_doubletalk_synth.h` DECtalk callback | Treated the callback's second argument as a sample count and looped over it. The engine passes 0 for audio blocks (3 = index mark); the block size is implicit (71/51). The engine would have produced no audio. | `engine_dectalk.c` follows `dtalkd/engine_dectalk.c`: fixed block size, code 3 skipped, never returns NULL. |
| 2 | ESPK externs | Declared `espk_init_xip(...)`, `espk_speak_stream(text, sink)`; the real API is `espk_init(file, cfg)`, `espk_speak_stream(text)` with a global `wgout_sink` of unsigned 8-bit samples. Link failure. | `engine_espk.c` uses the real API; `espk_data_mem.c` serves `ESPK.DAT` from flash in place. |
| 3 | DECtalk build | Missing `-DCHEESY_DICT_COMPRESSION` and used the 4,809-word subset dictionary; the working DOS build needs the define and the full dictionary or every word gets its own pitch accent. | Build flags and `build-gen/maindict.c` taken from `accessible_os/freedos/src/dectalk/Makefile`. |
| 4 | `SPEAK.COM` | `JR Z` to the usage path landed inside a `CALL` instruction (off by 3); the usage message address was 8 bytes early (pointed into code). `SPEAK` with no arguments printed nothing (confirmed in z80pack `cpmsim`). The documented `-s/-p/-v` options did not exist. | Rewritten in assembly (`speak.asm`, z80pack `z80asm`), with `/Sn /Pnn /Vn /On /X`, verified in `cpmsim` with a test build that prints the byte stream. |
| 4b | `SPEAK.COM` (rewrite) | Found later by running it under RunCPM: it set up its own stack and then ended with `RET`, popping uninitialised memory; on z80pack's zero-filled RAM that happened to warm-boot, on RunCPM it looped forever. Both exits are now `JP 0000h`. Verified on RunCPM (with the project-2 BDOS 224 hook: `^A7S^A60PHELLO FROM CP M` arrives) and on cpmsim. |
| 5 | Header-only design | DECtalk's `port.h` `#define volatile` (empty) would have stripped `volatile` from the ring buffers in any TU that included both. | `.h`/`.c` split; the engine glue is its own TU and includes `doubletalk_core1.h` first. |

### Races and protocol errors
| # | Problem | Fix |
| :-- | :-- | :-- |
| 6 | On `^X` Core 0 wrote the consumer's ring indices (`g_audio_rpos = g_audio_wpos`, `g_dt_fifo_rpos = ...`). Writing another core's index in an SPSC ring is a race. | Atomic flush counter + write-position snapshot; each consumer acknowledges on its own core. |
| 7 | `synth_render_utterance` cleared the flush flags at its start: a `^X` between parse and render was lost. | Render snapshots the counter, drops the clause if a flush is pending, and waits for the audio consumer's acknowledgement before pushing. |
| 8 | Two producers (Core 0 emulator and Core 1 screen reader) wrote one SPSC FIFO. | Screen reader uses a Core-1-only `core1_say` queue. |
| 9 | Hotkey handler ran on Core 0 and touched Core 1's screen-reader state. | Routing decision on Core 0 (pure function), action on Core 1 via a key FIFO. |
| 10 | Synthesis blocked the Core 1 loop while the ring was full, so the 30 ms screen diff could not run during speech. | Idle hook called from the ring-full wait runs the screen reader. |
| 11 | A key's flush (Core 0) was acknowledged after Core 1 had queued the key echo, deleting the echo (found by the host test). | `doubletalk_key_pop` acknowledges pending flushes first. |
| 12 | `^A` inside a numeric command was swallowed instead of restarting the command; commands without a number leaked their letter into speech. | Parser copied from `dtalkd/synth.c`. |
| 13 | No idle rule: a clause without CR was never spoken. | 60 ms idle rule, 200-byte buffer split at the last space. |

### Screen reader logic
| # | Problem | Fix |
| :-- | :-- | :-- |
| 14 | Apple II flashing range decoded as `raw & 0x7F`: a flashing cursor (`$60`) read as "grave", `$7F` as DEL; no `ALTCHARSET`/MouseText handling although the plan claimed it. | Flashing maps to the inverse-set glyphs; `ALTCHARSET` parameter; MouseText treated as decoration. |
| 15 | CGA highlight heuristic `bg > fg || bg == 7` missed white-on-blue bars (the common case) and marked nothing inside a full inverse bar. | Cell is highlighted when its background differs from the row's dominant background (rust-tdsr's rule); blink bit ignored. |
| 16 | Every changed row was read: a scroll re-read the whole screen, each typed key re-read the cursor line after the key echo, a repaint read 25 lines. | Scroll compensation, echo suppression within 400 ms of a key, console-output window between old and new cursor row, repaint silence. Two false positives found by tests (identical lines detected as a scroll; cursor jumping upward counted as console output) and fixed. |
| 17 | No CP/M VRAM adapter existed although Talking CP/M was the flagship personality. | `tdsr_capture_termcells` consumes the HDMI card's `TermCell` grid. |
| 18 | Symbol expansion produced double blanks; no run condensing ("=====" read as five "equals"). | Fixed; "11 equals Title 11 equals". |
| 19 | Panic chord text said "Rate 225" with a 225 wpm mapping that disagreed with `dtalkd` (200). | 100 + 20·s, message states the rate. |

### Plan facts
| # | Claim | Reality |
| :-- | :-- | :-- |
| 20 | SRAM "10 × 52 KB banks" | 8 × 64 KB + 2 × 4 KB. |
| 21 | DECtalk "510 KB flash, 160 KB dictionary" | 667 KB with the required full dictionary (329 KB). |
| 22 | ESPK "10.0 % WER, better than DECtalk" | That was `small.en` at +3 dB; `base.en` is 26–30 % vs DECtalk 12.4 %. |
| 23 | "ESPK 110 KB SRAM, DECtalk 58 KB" | ESPK 79 KB bss + 12 KB heap + 47.5 KB stack; DECtalk 54 KB + 2.3 KB stack + 0 heap. |
| 24 | Volume default 8, "resets volume to 100 %" | dtalkd default 5 (80 %); panic resets to 5. |
| 25 | "300 ms spoken boot" was asserted | The figure was arbitrary; the owner's requirement is a quick *and reliable* start (under about a second). Measured: chime within milliseconds, first 743 ms of audio ready ~26 ms after Core 1 start (QEMU floor). The DOS warm-up (370 ms) is kept but moved to run after the boot message is in the ring, while it plays: the compiled DECtalk files contain `malloc` calls on some text paths, so the warm-up makes those one-time allocations happen at a known moment. Added: engine-independent chime, DECtalk → ESPK → tone fallback on init failure or silent output, Core 1 heartbeat watchdog with relaunch in the firmware. |
| 26 | Core 1 stack unmentioned | Pico SDK default is 2 KB; ESPK needs 47.5 KB. Firmware allocates 64 KB. |
| 27 | HDMI data-island audio described as done | Not implemented; PWM headphone path is. |

## Measurements (QEMU mps2-an505, Cortex-M33, `-icount shift=0`, `-O2`)

Calibration: a 2,000,000-instruction loop measured 40,000 SysTick ticks at 20 MHz (50 instructions per tick, as expected for 1 ns per instruction).

| Component | text | data | bss |
| :-- | --: | --: | --: |
| DECtalk engine code (67 files + glue) | 318,113 | 19,594 | 33,700 |
| DECtalk full US dictionary | 328,763 | 0 | 0 |
| ESPK code + tables | 136,396 | 84 | 78,670 |
| ESPK.DAT | 378,432 | 0 | 0 |
| Core 1 speech core + screen reader | 11,998 | 190 | 33,205 |

| Run | Result |
| :-- | :-- |
| DECtalk init | 0.02 M instructions, heap 0 |
| Boot: chime + message, first ring fill (8192 samples) | 3.27 M instructions (21.8 ms at 150 MHz) |
| Boot message 4.46 s audio + post-boot DECtalk warm-up | 84.7 M instructions (warm-up 62 M, heap stays 0) |
| DECtalk sentence, 5.35 s audio | 28.4 M instructions = 5.31 M per audio second = 3.5 % of 150 MHz (28×) |
| ESPK init (lazy) | 0.4 M instructions, heap 12,288 |
| ESPK sentence, 5.19 s audio | 13.8 M = 2.66 M per audio second = 1.8 % (56×) |
| Stack high-water | DECtalk 2,344 B; ESPK 47,528 B |
| Flush at 400 ms | 4,416 samples before `^X`, 0 after, next utterance plays |
| WAV comparison | `arm_sentence_dectalk.wav`, `arm_sentence_espk.wav`, `arm_boot_dectalk.wav` byte-identical to the host build |

Pico SDK image (`pico_firmware/build/talking_retro.elf`): text 1,276,924, bss 220,316 (incl. 64 KB Core 1 stack); `talking_retro.uf2` 2,554,880 bytes.

Whisper `base.en` (same WAVs on host and ARM):
- `sentence_dectalk.wav`: "The quick round fox jumps over the lazy dog and then it runs away into the forest."
- `sentence_espk.wav`: "The quick round box jumps over the lazy dog, and then it runs away into the forest."
- `boot_dectalk.wav`: "Deck talk ready. Talking ZPM. All age for help."
- `boot_espk.wav`: "DSP, CLUT READY, TALKING, CPM, OCH, FOR HELL." (Whisper in low-confidence capitals: the 8-bit ESPK voice is weak on this short, name-heavy message even after renaming the engine "E speak Klatt"; its sentence transcript above is fine. A 16-bit ESPK output path and a plainer boot text are open items.)
- `flush_dectalk.wav` (0.40 s): "This is..."

## Tooling set up for this (no sudo available)
`/home/ccd/retro_pc_project/tools/`: Arm GNU Toolchain 13.3.rel1 (`arm-gnu-toolchain/`),
QEMU 8.2 `qemu-system-arm` extracted from the Ubuntu packages (`qemu/usr/bin`, data in
`qemu/usr/share/qemu`), Pico SDK 2.1.1 (`pico-sdk/`, shallow clone, no submodules).
z80pack's `cpmsim` and `z80asm` were already present (`/home/ccd/z80pack`,
`/usr/local/bin/z80asm`).  Whisper via `uv run host_test/tools/transcribe_np.py`
(faster-whisper, numpy loader because the PyAV decoder in that environment is
incompatible).
