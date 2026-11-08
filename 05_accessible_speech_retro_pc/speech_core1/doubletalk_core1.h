/*
 * doubletalk_core1.h
 *
 * RP2350 Core 1 "Virtual DoubleTalk LT" speech coprocessor: public API.
 *
 * Core 0 (the emulated Z80 / 65C02 / 8088 machine) writes bytes to a serial
 * synthesizer port (COM2 0x2F8, CP/M BDOS 224, Apple Slot 4 $C0C0) and key
 * events.  Core 1 parses the RC Systems DoubleTalk command set, renders speech
 * with DECtalk (ARM7 configuration), ESPK (eSpeak-NG + klatt_fx) or a
 * diagnostic tone voice, and feeds one 16-bit PCM ring that both audio
 * outputs (HDMI data islands and the GPIO 20/21 headphone PWM) consume.
 *
 * Threading contract (every function says which core may call it):
 *   - "Core 0" functions are the only producers of the text FIFO and key FIFO.
 *   - "Core 1" functions run in Core 1's main loop (never from an ISR).
 *   - "audio consumer" functions run in Core 1's audio ISR / DMA refill (or the
 *     host test harness).  They are the only writer of the PCM ring read index.
 *   - A flush (^X) is a monotonically increasing counter g_dt_flush_req that any
 *     core may bump with an atomic add; consumers compare it with their own
 *     acknowledgement counter.  No core ever writes another core's ring index.
 *
 * Why this is a .h/.c pair and not a header-only file: the DECtalk ARM7 port
 * header (include/port.h) does "#define volatile" to nothing.  Any translation
 * unit that includes epsonapi.h therefore loses every volatile qualifier that
 * comes after it, which silently breaks lock-free ring buffers.  The DECtalk
 * glue lives in engine_dectalk.c and never sees these ring buffers' source.
 */
#ifndef DOUBLETALK_CORE1_H
#define DOUBLETALK_CORE1_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */
#ifndef DT_AUDIO_RING_SAMPLES
#define DT_AUDIO_RING_SAMPLES 8192          /* 16 KB: 743 ms at 11025 Hz */
#endif
#ifndef DT_TEXT_FIFO_SIZE
#define DT_TEXT_FIFO_SIZE     2048          /* Core 0 -> Core 1 serial bytes */
#endif
#ifndef DT_KEY_FIFO_SIZE
#define DT_KEY_FIFO_SIZE      64            /* Core 0 -> Core 1 key events */
#endif
#define DT_UTT_MAX            200           /* DoubleTalk clause, as in dtalkd/synth.c */
#define DT_SAY_MAX            480           /* Core 1 screen reader utterance */
#define DT_SAY_QUEUE          8
#define DT_IDLE_SPEAK_MS      60            /* speak a clause without CR after this idle time */
#ifndef DT_POST_BOOT_WARMUP
#define DT_POST_BOOT_WARMUP   1             /* run the engine warm-up right after the boot message */
#endif
#define DT_CHIME_TEXT         "\a"          /* a core1_say() of this plays the boot chime */

typedef enum {
    SYNTH_ENGINE_DECTALK = 0,   /* DECtalk 4.x ARM7 (epsonapi.c), 11025/8000 Hz 16-bit */
    SYNTH_ENGINE_ESPK    = 1,   /* eSpeak-NG English front end + klatt_fx, 8-bit output */
    SYNTH_ENGINE_TONE    = 2,   /* diagnostic tone-per-character voice, always available */
    SYNTH_ENGINE_COUNT   = 3
} synth_engine_id_t;

typedef struct {
    uint8_t engine;        /* synth_engine_id_t */
    uint8_t speed_0_9;     /* DoubleTalk ^A<0..9>S, default 5 */
    uint8_t pitch_0_99;    /* DoubleTalk ^A<0..99>P, default 50 */
    uint8_t volume_0_9;    /* DoubleTalk ^A<0..9>V, default 5 (dtalkd: 5 = 80 %) */
    uint8_t voice_0_7;     /* DoubleTalk ^A<0..7>O: Paul Betty Harry Frank Dennis Kit Ursula Rita */
} dt_voice_state_t;

typedef struct {
    uint32_t utterances;        /* clauses handed to an engine */
    uint32_t flushes;           /* ^X requests seen by the parser */
    uint32_t aborted;           /* utterances cut short by a flush */
    uint32_t text_overruns;     /* bytes dropped because the text FIFO was full */
    uint32_t key_overruns;
    uint32_t ring_underruns;    /* audio pops while the ring was empty during an utterance */
    uint32_t samples_out;       /* samples popped by the audio consumer */
    uint32_t engine_failures;   /* an engine failed to initialise or produced no audio */
    uint32_t fallbacks;         /* utterances re-rendered with the next engine */
    uint32_t warmups;           /* engine warm-ups run */
} dt_stats_t;

/* Owned by Core 1; Core 0 may read it (e.g. to show the voice settings). */
extern dt_voice_state_t g_dt_state;
extern dt_stats_t       g_dt_stats;
extern volatile uint32_t g_dt_flush_req;    /* bumped by any core on ^X */

/* ------------------------------------------------------------------------ */
/* Core 0 side: the emulated machine's synthesizer port                      */
/* ------------------------------------------------------------------------ */

/* One byte written to the synthesizer (OUT 0x2F8 / INT 14h AH=1 / BDOS 224 /
 * STA $C0C0).  ^X (0x18) is handled at once: the audio ring is silenced by the
 * next audio pop (<= one sample period) and the clause being spoken is
 * dropped by the synthesizer.  Lock-free, never blocks; a full FIFO drops the
 * byte and counts an overrun. */
void     doubletalk_port_write_byte(uint8_t b);
void     doubletalk_write_string(const char *s);      /* bytes up to NUL (BDOS 224) */

/* INT 14h AH=03h result: AH = line status 0x60 (THRE|TEMT), AL = modem status
 * 0xB0 (DCD|DSR|CTS).  Always ready, so screen readers never wait on baud time. */
uint16_t doubletalk_port_status(void);

/* 16550 register-level view for PC emulators trapping IN/OUT at 0x2F8..0x2FF:
 * reg 0 = THR/RBR, 1 = IER, 2 = IIR, 3 = LCR, 4 = MCR, 5 = LSR, 6 = MSR, 7 = SCR. */
uint8_t  doubletalk_uart_read(uint8_t reg);
void     doubletalk_uart_write(uint8_t reg, uint8_t v);

/* Key event from the emulator's keyboard path, before the emulated OS sees it.
 * Returns 1 when the screen reader consumes the key (an Alt+key review command
 * or the Alt+Shift+Backspace panic chord): do not deliver it to the emulated
 * machine.  Any other key is passed through (returns 0) and also silences
 * speech at once and is echoed by the screen reader when key echo is on. */
#define DT_MOD_SHIFT 0x01
#define DT_MOD_CTRL  0x02
#define DT_MOD_ALT   0x04
int      doubletalk_key_event(uint8_t ascii, uint8_t mods);

/* ------------------------------------------------------------------------ */
/* Core 1 side: synthesizer main loop                                        */
/* ------------------------------------------------------------------------ */
void core1_speech_init(synth_engine_id_t engine, int sample_rate);
int  core1_speech_sample_rate(void);
bool core1_engine_available(synth_engine_id_t engine);

/* Power-on sequence, queued first thing on Core 1 and rendered by the following
 * core1_speech_poll() calls: a short two-tone chime that needs no engine (so a
 * broken engine still gives an audible sign of life), then the spoken message.
 * With DT_POST_BOOT_WARMUP the DECtalk warm-up (dtalkd's two sentences, ~55 M
 * instructions) runs right after the message has been handed to the ring, i.e.
 * while its last 743 ms are still playing: every lazily allocated table then
 * exists before the emulated machine starts talking, at no audible cost. */
void core1_speech_announce_boot(const char *mode_name);
void core1_chime(void);                /* queue the chime alone */

/* Reliability: an engine that fails to initialise or renders a clause with no
 * audio is marked failed and the clause is re-rendered with the next engine
 * (DECtalk -> ESPK -> tone).  The firmware's watchdog path can mark one too. */
void core1_mark_engine_failed(synth_engine_id_t engine);

/* Parse pending serial bytes, render due clauses and queued Core 1 utterances.
 * Rendering blocks inside this call while the PCM ring is full; the idle hook
 * runs during those waits so the screen reader keeps running. */
void core1_speech_poll(uint32_t now_ms);
void core1_set_idle_hook(void (*hook)(void));

/* Core 1 originated speech (the hypervisor screen reader).  Not parsed as a
 * DoubleTalk stream.  flush_first drops everything queued or playing. */
void core1_say(const char *text, bool flush_first);
void core1_flush(void);
bool core1_speech_idle(void);          /* nothing queued, parsed or playing */

/* Key events popped by the screen reader on Core 1. */
bool doubletalk_key_pop(uint8_t *ascii, uint8_t *mods);

/* ------------------------------------------------------------------------ */
/* Audio consumer: Core 1 audio ISR / DMA refill / host harness              */
/* ------------------------------------------------------------------------ */
int16_t  audio_output_pop_sample(void);                 /* 0 when idle */
int      audio_output_pop_block(int16_t *dst, int n);   /* returns real samples, rest padded 0 */
uint32_t audio_output_pending(void);                    /* samples waiting in the ring */

/* ------------------------------------------------------------------------ */
/* Engine side (called from engine_*.c on Core 1 while rendering)            */
/* ------------------------------------------------------------------------ */
/* Push synthesized audio.  Returns false when the utterance was flushed: the
 * engine should stop producing (ESPK: raise wgout_abort; DECtalk: keep
 * returning its buffer but discard, never return NULL). */
bool synth_push_pcm16(const int16_t *pcm, int n);
bool synth_push_pcm8u(const uint8_t *pcm, int n);       /* unsigned 8-bit, 128 = silence */
bool synth_flush_pending(void);

/* Engines, each in its own translation unit (weak no-op stubs are provided):
 *   engine_dectalk.c  (needs the DECtalk ARM7 objects)
 *   engine_espk.c     (needs vintage-pc-speech/src/espeak/lib + espk + klatt_fx) */
int  engine_dectalk_init(int sample_rate);
void engine_dectalk_warmup(void);                       /* optional, see DT_POST_BOOT_WARMUP */
void engine_dectalk_speak(const char *text);            /* text already carries [:ra][:dv] */
int  engine_espk_init(int sample_rate);                 /* includes ESPK's warm-up (lazy, first use) */
void engine_espk_speak(const char *text, int wpm, int pitch_0_99);

/* DoubleTalk -> engine parameter mapping (shared with the tests) */
int  dt_speed_to_dectalk_wpm(int speed_0_9);     /* 100 + 20*s  (5 = 200 wpm) */
int  dt_pitch_to_dectalk_ap_hz(int pitch_0_99);  /* 60 + 1.25*p (50 = 122 Hz) */
int  dt_speed_to_espk_wpm(int speed_0_9);        /* 175 * (100 + 8*(s-7)) / 100 */
int  dt_volume_to_q8(int volume_0_9);            /* {0,20,...,100} % as Q8 */

/* Observability for tests and logging: called with every utterance text that
 * reaches an engine (after a flush check), and with the engine used. */
typedef void (*synth_render_observer_t)(synth_engine_id_t engine, const char *text);
void core1_set_render_observer(synth_render_observer_t fn);

#ifdef __cplusplus
}
#endif
#endif /* DOUBLETALK_CORE1_H */
