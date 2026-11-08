/*
 * doubletalk_core1.c -- RP2350 Core 1 Virtual DoubleTalk LT speech coprocessor.
 * See doubletalk_core1.h for the threading contract.
 *
 * The DoubleTalk byte-stream parser is the one proven with Provox 7 in
 * vintage-pc-speech (src/dectalk/dtalkd/synth.c): an utterance ends at CR, LF,
 * NUL, when the input goes idle, or at the last space when the 200-byte buffer
 * is full; never at '.', '?' or '!' (DECtalk shapes whole sentences itself and
 * would give "3.5" or "Mr. Smith" a final fall).
 */
#include <string.h>
#include <stdio.h>
#include "doubletalk_core1.h"

#define AUDIO_MASK (DT_AUDIO_RING_SAMPLES - 1)
#define TXT_MASK   (DT_TEXT_FIFO_SIZE - 1)
#define KEY_MASK   (DT_KEY_FIFO_SIZE - 1)

#if (DT_AUDIO_RING_SAMPLES & AUDIO_MASK) || (DT_TEXT_FIFO_SIZE & TXT_MASK) || (DT_KEY_FIFO_SIZE & KEY_MASK)
#error "ring sizes must be powers of two"
#endif

#define LOAD(p)      __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define STORE(p, v)  __atomic_store_n((p), (v), __ATOMIC_RELEASE)

/* ---- shared state ------------------------------------------------------- */
dt_voice_state_t g_dt_state = { SYNTH_ENGINE_DECTALK, 5, 50, 5, 0 };
dt_stats_t       g_dt_stats;
volatile uint32_t g_dt_flush_req  = 0;     /* bumped on every ^X, any core */
static volatile uint32_t g_dt_flush_wpos = 0; /* text FIFO position at the last ^X */

/* PCM ring: written by Core 1 main (synth), read by the audio consumer. */
static int16_t           g_audio_ring[DT_AUDIO_RING_SAMPLES];
static volatile uint32_t g_audio_wpos = 0;
static volatile uint32_t g_audio_rpos = 0;
static volatile uint32_t g_audio_ack  = 0;   /* flush counter acknowledged by the consumer */

/* Serial text FIFO: written by Core 0, read by Core 1. */
static uint8_t           g_txt[DT_TEXT_FIFO_SIZE];
static volatile uint32_t g_txt_w = 0;
static volatile uint32_t g_txt_r = 0;

/* Key event FIFO: written by Core 0, read by Core 1. */
static uint8_t           g_key_a[DT_KEY_FIFO_SIZE];
static uint8_t           g_key_m[DT_KEY_FIFO_SIZE];
static volatile uint32_t g_key_w = 0;
static volatile uint32_t g_key_r = 0;

/* Core 1 private state */
static int      g_rate = 11025;
static int8_t   g_engine_state[SYNTH_ENGINE_COUNT];   /* 0 untried, 1 ready, -1 failed: engines initialise on first use */
static void   (*g_idle_hook)(void) = NULL;
static synth_render_observer_t g_observer = NULL;
static uint32_t g_parse_ack = 0;             /* flush counter handled by the parser */
static volatile uint32_t g_render_base = 0;  /* flush counter when the current utterance began */
static volatile bool g_rendering = false;
static uint32_t g_pushed_this_utt = 0;       /* samples the engine delivered for the current utterance */
static bool     g_warmup_pending = false;

static char     g_utt[DT_UTT_MAX + 8];
static unsigned g_utt_len = 0;
static bool     g_in_cmd = false, g_cmd_has_val = false;
static unsigned g_cmd_val = 0;
static uint32_t g_last_byte_ms = 0;
static bool     g_have_time = false;

static char     g_say[DT_SAY_QUEUE][DT_SAY_MAX + 1];
static uint8_t  g_say_flush[DT_SAY_QUEUE];
static unsigned g_say_w = 0, g_say_r = 0;

static bool ensure_engine(synth_engine_id_t e);

/* 16550 shadow registers for the PC personality */
static uint8_t g_uart_regs[8] = { 0, 0, 0x01, 0x03, 0, 0x60, 0xB0, 0 };

/* ---- parameter maps (dtalkd/engine_dectalk.c, espkd/engine_espk.c) ----- */
int dt_speed_to_dectalk_wpm(int s)   { if (s < 0) s = 0; if (s > 9) s = 9; return 100 + 20 * s; }
int dt_pitch_to_dectalk_ap_hz(int p) { if (p < 0) p = 0; if (p > 99) p = 99; return 60 + (p * 5) / 4; }
int dt_speed_to_espk_wpm(int s)      { if (s < 0) s = 0; if (s > 9) s = 9; return 175 * (100 + (s - 7) * 8) / 100; }
int dt_volume_to_q8(int v)
{
    static const uint8_t pct[10] = { 0, 20, 35, 50, 65, 80, 85, 90, 95, 100 };
    if (v < 0) v = 0;
    if (v > 9) v = 9;
    return (pct[v] * 256) / 100;
}

/* ---- weak engine stubs: the core links and talks (tone voice) without them */
__attribute__((weak)) int  engine_dectalk_init(int sample_rate) { (void)sample_rate; return -1; }
__attribute__((weak)) void engine_dectalk_warmup(void) {}
__attribute__((weak)) void engine_dectalk_speak(const char *text) { (void)text; }
__attribute__((weak)) int  engine_espk_init(int sample_rate) { (void)sample_rate; return -1; }
__attribute__((weak)) void engine_espk_speak(const char *text, int wpm, int pitch) { (void)text; (void)wpm; (void)pitch; }

/* ======================================================================== */
/* Core 0 side                                                               */
/* ======================================================================== */
static inline void request_flush(void)
{
    STORE(&g_dt_flush_wpos, LOAD(&g_txt_w));          /* text before here is stale */
    __atomic_add_fetch(&g_dt_flush_req, 1, __ATOMIC_SEQ_CST);
}

void doubletalk_port_write_byte(uint8_t b)
{
    uint32_t w, next;
    if (b == 0x18) { request_flush(); return; }
    w = g_txt_w;
    next = (w + 1) & TXT_MASK;
    if (next == LOAD(&g_txt_r)) { g_dt_stats.text_overruns++; return; }
    g_txt[w] = b;
    STORE(&g_txt_w, next);
}

void doubletalk_write_string(const char *s)
{
    while (*s) doubletalk_port_write_byte((uint8_t)*s++);
}

uint16_t doubletalk_port_status(void) { return 0x60B0; }

uint8_t doubletalk_uart_read(uint8_t reg)
{
    reg &= 7;
    switch (reg) {
        case 0: return 0;                 /* RBR: the synthesizer never talks back */
        case 2: return 0x01;              /* IIR: no interrupt pending */
        case 5: return 0x60;              /* LSR: THRE | TEMT */
        case 6: return 0xB0;              /* MSR: DCD | DSR | CTS */
        default: return g_uart_regs[reg];
    }
}

void doubletalk_uart_write(uint8_t reg, uint8_t v)
{
    reg &= 7;
    if (reg == 0) {
        if (g_uart_regs[3] & 0x80) { g_uart_regs[0] = v; return; }   /* DLAB: divisor latch, ignore */
        doubletalk_port_write_byte(v);
        return;
    }
    g_uart_regs[reg] = v;
}

/* Screen reader hotkeys: Alt + one of these, or Alt+Shift+Backspace. */
static bool is_reader_hotkey(uint8_t ascii, uint8_t mods)
{
    uint8_t c = (ascii >= 'a' && ascii <= 'z') ? (uint8_t)(ascii - 32) : ascii;
    if (!(mods & DT_MOD_ALT)) return false;
    if ((mods & DT_MOD_SHIFT) && (ascii == 0x08 || ascii == 0x7F)) return true;
    return strchr("UIOJKLM,.<>CRSHX=+-", (int)c) != NULL && c != 0;
}

int doubletalk_key_event(uint8_t ascii, uint8_t mods)
{
    uint32_t w = g_key_w, next = (w + 1) & KEY_MASK;
    bool consumed = is_reader_hotkey(ascii, mods);
    if (!(mods & DT_MOD_ALT)) request_flush();        /* any ordinary key silences speech */
    if (next == LOAD(&g_key_r)) { g_dt_stats.key_overruns++; return consumed; }
    g_key_a[w] = ascii;
    g_key_m[w] = mods;
    STORE(&g_key_w, next);
    return consumed;
}

static void parser_apply_flush(void);

bool doubletalk_key_pop(uint8_t *ascii, uint8_t *mods)
{
    uint32_t r = g_key_r;
    /* Core 1 only.  The flush that Core 0 requested with this key must be
     * acknowledged before the screen reader queues the key's echo, or the
     * next poll would sweep the echo away with the stale speech. */
    parser_apply_flush();
    if (r == LOAD(&g_key_w)) return false;
    *ascii = g_key_a[r];
    *mods  = g_key_m[r];
    STORE(&g_key_r, (r + 1) & KEY_MASK);
    return true;
}

/* ======================================================================== */
/* Audio consumer                                                            */
/* ======================================================================== */
static inline void audio_apply_flush(void)
{
    uint32_t req = LOAD(&g_dt_flush_req);
    if (req != g_audio_ack) {
        g_audio_rpos = LOAD(&g_audio_wpos);           /* drop everything queued: instant silence */
        STORE(&g_audio_ack, req);
    }
}

int16_t audio_output_pop_sample(void)
{
    uint32_t r;
    int16_t s;
    audio_apply_flush();
    r = g_audio_rpos;
    if (r == LOAD(&g_audio_wpos)) {
        if (g_rendering) g_dt_stats.ring_underruns++;
        return 0;
    }
    s = g_audio_ring[r];
    STORE(&g_audio_rpos, (r + 1) & AUDIO_MASK);
    g_dt_stats.samples_out++;
    return s;
}

int audio_output_pop_block(int16_t *dst, int n)
{
    int got = 0;
    audio_apply_flush();
    while (got < n) {
        uint32_t r = g_audio_rpos;
        if (r == LOAD(&g_audio_wpos)) break;
        dst[got++] = g_audio_ring[r];
        STORE(&g_audio_rpos, (r + 1) & AUDIO_MASK);
    }
    g_dt_stats.samples_out += (uint32_t)got;
    if (got < n) memset(dst + got, 0, (size_t)(n - got) * sizeof(int16_t));
    return got;
}

uint32_t audio_output_pending(void)
{
    return (LOAD(&g_audio_wpos) - LOAD(&g_audio_rpos)) & AUDIO_MASK;
}

/* ======================================================================== */
/* Engine side                                                               */
/* ======================================================================== */
bool synth_flush_pending(void)
{
    return LOAD(&g_dt_flush_req) != g_render_base;
}

static bool push_one(int32_t scaled)
{
    uint32_t w = g_audio_wpos, next = (w + 1) & AUDIO_MASK;
    while (next == LOAD(&g_audio_rpos)) {             /* ring full: let the screen reader run */
        if (synth_flush_pending()) return false;
        if (!g_idle_hook) { g_dt_stats.ring_underruns++; return true; } /* nobody drains: drop */
        g_idle_hook();
    }
    if (scaled > 32767) scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    g_audio_ring[w] = (int16_t)scaled;
    STORE(&g_audio_wpos, next);
    g_pushed_this_utt++;
    return true;
}

bool synth_push_pcm16(const int16_t *pcm, int n)
{
    int q8 = dt_volume_to_q8(g_dt_state.volume_0_9);
    for (int i = 0; i < n; i++) {
        if (synth_flush_pending()) return false;
        if (!push_one(((int32_t)pcm[i] * q8) >> 8)) return false;
    }
    return true;
}

bool synth_push_pcm8u(const uint8_t *pcm, int n)
{
    int q8 = dt_volume_to_q8(g_dt_state.volume_0_9);
    for (int i = 0; i < n; i++) {
        if (synth_flush_pending()) return false;
        if (!push_one((((int32_t)pcm[i] - 128) << 8) * q8 >> 8)) return false;
    }
    return true;
}

/* ======================================================================== */
/* Core 1: rendering                                                         */
/* ======================================================================== */
static void tone_voice(const char *text)
{
    /* Diagnostic voice: one short square-wave burst per character, pitch from
     * the character code, so a build without engines still proves the audio
     * path end to end (and a blind user hears that the board is alive). */
    int wpm = dt_speed_to_dectalk_wpm(g_dt_state.speed_0_9);
    int burst = (g_rate * 8) / wpm;           /* 40 ms per character at 200 wpm */
    int16_t buf[64];
    for (const uint8_t *p = (const uint8_t *)text; *p; p++) {
        int c = *p, n = burst, phase = 0, step;
        if (c <= ' ') {
            memset(buf, 0, sizeof buf);
            for (int left = g_rate / 25; left > 0; left -= 64)
                if (!synth_push_pcm16(buf, left > 64 ? 64 : left)) return;
            continue;
        }
        step = (65536 * (200 + (c & 0x1F) * 14)) / g_rate;
        while (n > 0) {
            int k = n > 64 ? 64 : n;
            for (int i = 0; i < k; i++) {
                phase = (phase + step) & 0xFFFF;
                buf[i] = (phase < 0x8000) ? 6000 : -6000;
            }
            if (!synth_push_pcm16(buf, k)) return;
            n -= k;
        }
    }
}

/* Two-tone boot chime (E5 then A5, 70 ms each) straight into the ring: no engine,
 * no dictionary, nothing that can fail.  If this is not heard, the audio path or
 * the board is dead, not the synthesizer. */
static void chime(void)
{
    static const int freqs[2] = { 659, 880 };
    int16_t buf[64];
    for (int t = 0; t < 2; t++) {
        int n = (g_rate * 70) / 1000, phase = 0, step = (65536 * freqs[t]) / g_rate;
        while (n > 0) {
            int k = n > 64 ? 64 : n;
            for (int i = 0; i < k; i++) {
                phase = (phase + step) & 0xFFFF;
                buf[i] = (int16_t)((phase < 0x8000 ? 5000 : -5000) * (n > 64 ? 1 : n) / (n > 64 ? 1 : 64)); /* short fade-out */
            }
            if (!synth_push_pcm16(buf, k)) return;
            n -= k;
        }
    }
}

static bool text_has_content(const char *t)
{
    for (; *t; t++) if ((*t >= '0' && *t <= '9') || (*t >= 'A' && *t <= 'Z') || (*t >= 'a' && *t <= 'z')) return true;
    return false;
}

static const char *const k_dt_speakers[8] = { "np", "nb", "nh", "nf", "nd", "nk", "nu", "nr" };

static synth_engine_id_t next_engine_after(synth_engine_id_t e)
{
    for (int i = 1; i < SYNTH_ENGINE_COUNT; i++) {
        synth_engine_id_t c = (synth_engine_id_t)((e + i) % SYNTH_ENGINE_COUNT);
        if (ensure_engine(c)) return c;
    }
    return SYNTH_ENGINE_TONE;
}

void core1_mark_engine_failed(synth_engine_id_t e)
{
    if (e >= SYNTH_ENGINE_COUNT || e == SYNTH_ENGINE_TONE) return;
    if (g_engine_state[e] != -1) g_dt_stats.engine_failures++;
    g_engine_state[e] = -1;
    if (g_dt_state.engine == e) g_dt_state.engine = (uint8_t)next_engine_after(e);
}

static void render_with(synth_engine_id_t eng, const char *text);

static void render_text(const char *text)
{
    uint32_t base = LOAD(&g_dt_flush_req);
    synth_engine_id_t eng = (synth_engine_id_t)g_dt_state.engine;

    if (!text[0]) return;
    if (base != g_parse_ack) return;                  /* a ^X arrived after this clause was assembled */
    if (text[0] == '\a' && !text[1]) {               /* the boot chime */
        /* same rule as render_with: the audio consumer must have acknowledged the
         * last flush before new samples enter the ring, or it drops them */
        if (g_idle_hook) while (LOAD(&g_audio_ack) != base) g_idle_hook();
        g_render_base = base; g_rendering = true;
        if (g_observer) g_observer(SYNTH_ENGINE_TONE, text);
        chime();
        g_rendering = false;
        return;
    }
    if (!ensure_engine(eng)) eng = SYNTH_ENGINE_TONE;
    render_with(eng, text);
    /* no audio from a clause that has letters or digits: the engine is broken; try the next one */
    if (g_pushed_this_utt == 0 && LOAD(&g_dt_flush_req) == base && eng != SYNTH_ENGINE_TONE && text_has_content(text)) {
        core1_mark_engine_failed(eng);
        g_dt_stats.fallbacks++;
        render_with(next_engine_after(eng), text);
    }
}

static void render_with(synth_engine_id_t eng, const char *text)
{
    uint32_t base = LOAD(&g_dt_flush_req);

    /* Make sure the audio consumer has acknowledged the last flush before new
     * samples enter the ring, so its drop can never swallow this utterance. */
    if (g_idle_hook) while (LOAD(&g_audio_ack) != base) g_idle_hook();

    g_render_base = base;
    g_rendering   = true;
    g_pushed_this_utt = 0;
    g_dt_stats.utterances++;
    if (g_observer) g_observer(eng, text);

    switch (eng) {
        case SYNTH_ENGINE_DECTALK: {
            char buf[DT_SAY_MAX + 48];
            snprintf(buf, sizeof buf, "[:%s][:ra %d][:dv ap %d] %s",
                     k_dt_speakers[g_dt_state.voice_0_7 & 7],
                     dt_speed_to_dectalk_wpm(g_dt_state.speed_0_9),
                     dt_pitch_to_dectalk_ap_hz(g_dt_state.pitch_0_99), text);
            engine_dectalk_speak(buf);
            break;
        }
        case SYNTH_ENGINE_ESPK:
            engine_espk_speak(text, dt_speed_to_espk_wpm(g_dt_state.speed_0_9), g_dt_state.pitch_0_99);
            break;
        default:
            tone_voice(text);
            break;
    }
    g_rendering = false;
    if (LOAD(&g_dt_flush_req) != base) g_dt_stats.aborted++;
}

/* ======================================================================== */
/* Core 1: DoubleTalk stream parser                                          */
/* ======================================================================== */
static void speak_clause(void)
{
    if (!g_utt_len) return;
    g_utt[g_utt_len] = 0;
    g_utt_len = 0;
    render_text(g_utt);
}

static void speak_clause_full(void)
{
    /* buffer full: speak up to the last space, keep the word being received */
    char keep[DT_UTT_MAX];
    unsigned sp = g_utt_len, n;
    while (sp > 0 && g_utt[sp - 1] != ' ') sp--;
    if (sp == 0) { speak_clause(); return; }
    n = g_utt_len - sp;
    memcpy(keep, g_utt + sp, n);
    g_utt_len = sp;
    speak_clause();
    memcpy(g_utt, keep, n);
    g_utt_len = n;
}

static void do_command(uint8_t letter)
{
    unsigned v = g_cmd_has_val ? g_cmd_val : 0;
    switch (letter) {
        case 'S': case 's': g_dt_state.speed_0_9  = (uint8_t)(v > 9 ? 9 : v); break;
        case 'P': case 'p': g_dt_state.pitch_0_99 = (uint8_t)(v > 99 ? 99 : v); break;
        case 'V': case 'v': g_dt_state.volume_0_9 = (uint8_t)(v > 9 ? 9 : v); break;
        case 'O': case 'o': g_dt_state.voice_0_7  = (uint8_t)(v > 7 ? 7 : v); break;
        case '@':           g_dt_state.speed_0_9 = 5; g_dt_state.pitch_0_99 = 50; g_dt_state.volume_0_9 = 5; break;
        case 'Z': case 'z': /* extension: engine select, 0 DECtalk, 1 ESPK, 2 tone */
            if (v < SYNTH_ENGINE_COUNT && ensure_engine((synth_engine_id_t)v)) g_dt_state.engine = (uint8_t)v;
            break;
        default: break;     /* F, X, M, T, B, E, A, R, I ... accepted and ignored, like sbtalk */
    }
}

static void parser_apply_flush(void)
{
    uint32_t req = LOAD(&g_dt_flush_req);
    if (req == g_parse_ack) return;
    g_parse_ack = req;
    g_dt_stats.flushes++;
    {
        /* Drop the bytes that were queued before the ^X: advance our read index to
         * the producer's snapshot if that snapshot is still ahead of us. */
        uint32_t r = g_txt_r, w = LOAD(&g_txt_w), snap = LOAD(&g_dt_flush_wpos);
        uint32_t avail = (w - r) & TXT_MASK, to_snap = (snap - r) & TXT_MASK;
        if (to_snap <= avail) STORE(&g_txt_r, snap);
    }
    g_utt_len = 0;
    g_in_cmd = false;
    g_say_r = g_say_w = 0;
}

static void parse_byte(uint8_t c)
{
    if (g_in_cmd) {
        if (c >= '0' && c <= '9') { g_cmd_val = g_cmd_val * 10 + (c - '0'); g_cmd_has_val = true; }
        else if (c == 0x01) { g_cmd_val = 0; g_cmd_has_val = false; }      /* ^A^A: start over */
        else if (c == 0x18) { g_in_cmd = false; g_utt_len = 0; }           /* ^X inside a command */
        else { g_in_cmd = false; do_command(c); }
        return;
    }
    switch (c) {
        case 0x01: g_in_cmd = true; g_cmd_val = 0; g_cmd_has_val = false; break;
        case 0x18: g_utt_len = 0; break;
        case '\r': case '\n': case 0x00: speak_clause(); break;
        default:
            if (c < ' ' || c > 0x7E) break;
            if (g_utt_len < DT_UTT_MAX) g_utt[g_utt_len++] = (char)c;
            if (g_utt_len >= DT_UTT_MAX) speak_clause_full();
            break;
    }
}

/* ======================================================================== */
/* Core 1: public                                                            */
/* ======================================================================== */
void core1_set_idle_hook(void (*hook)(void)) { g_idle_hook = hook; }
void core1_set_render_observer(synth_render_observer_t fn) { g_observer = fn; }
int  core1_speech_sample_rate(void) { return g_rate; }
/* Initialise an engine the first time it is needed, so the boot message does not
 * wait for engines nobody asked for (ESPK init is ~17 M instructions). */
static bool ensure_engine(synth_engine_id_t e)
{
    if (e >= SYNTH_ENGINE_COUNT) return false;
    if (g_engine_state[e] == 0) {
        int r = (e == SYNTH_ENGINE_DECTALK) ? engine_dectalk_init(g_rate)
              : (e == SYNTH_ENGINE_ESPK)    ? engine_espk_init(g_rate) : 0;
        g_engine_state[e] = (r == 0) ? 1 : -1;
    }
    return g_engine_state[e] == 1;
}
bool core1_engine_available(synth_engine_id_t e) { return ensure_engine(e); }

void core1_speech_init(synth_engine_id_t engine, int sample_rate)
{
    g_rate = (sample_rate == 8000) ? 8000 : 11025;
    memset(&g_dt_stats, 0, sizeof g_dt_stats);
    memset(g_engine_state, 0, sizeof g_engine_state);
    g_engine_state[SYNTH_ENGINE_TONE] = 1;
    /* only the requested engine is initialised now; the others on first use */
    if (engine >= SYNTH_ENGINE_COUNT || !ensure_engine(engine)) {
        engine = ensure_engine(SYNTH_ENGINE_DECTALK) ? SYNTH_ENGINE_DECTALK
               : ensure_engine(SYNTH_ENGINE_ESPK)    ? SYNTH_ENGINE_ESPK : SYNTH_ENGINE_TONE;
    }
    g_dt_state.engine = (uint8_t)engine;
    g_parse_ack = g_audio_ack = g_render_base = LOAD(&g_dt_flush_req);
    g_utt_len = 0; g_in_cmd = false;
    g_say_r = g_say_w = 0;
}

static const char *engine_name(synth_engine_id_t e)
{
    switch (e) {
        case SYNTH_ENGINE_DECTALK: return "DECtalk";
        case SYNTH_ENGINE_ESPK:    return "E speak Klatt";
        default:                   return "test tone";
    }
}

void core1_chime(void) { core1_say(DT_CHIME_TEXT, false); }

void core1_speech_announce_boot(const char *mode_name)
{
    char msg[DT_SAY_MAX];
    snprintf(msg, sizeof msg, "%s ready. %s. Alt H for help.",
             engine_name((synth_engine_id_t)g_dt_state.engine), mode_name ? mode_name : "Talking CP M");
    core1_flush();
    core1_chime();
    core1_say(msg, false);
    g_warmup_pending = DT_POST_BOOT_WARMUP && g_engine_state[SYNTH_ENGINE_DECTALK] == 1;
}

void core1_flush(void)
{
    /* Core 1 is the parser, so acknowledge our own flush at once: the FIFO is
     * drained to the snapshot and the clause and say queue are cleared.  A
     * core1_say() that follows is then not swept away by the next poll. */
    request_flush();
    parser_apply_flush();
}

void core1_say(const char *text, bool flush_first)
{
    unsigned next;
    if (flush_first) core1_flush();
    if (!text || !text[0]) return;
    next = (g_say_w + 1) % DT_SAY_QUEUE;
    if (next == g_say_r) { g_say_r = (g_say_r + 1) % DT_SAY_QUEUE; }   /* overflow: drop oldest */
    strncpy(g_say[g_say_w], text, DT_SAY_MAX);
    g_say[g_say_w][DT_SAY_MAX] = 0;
    g_say_flush[g_say_w] = flush_first;
    g_say_w = next;
}

bool core1_speech_idle(void)
{
    return g_say_r == g_say_w && g_utt_len == 0 && LOAD(&g_txt_r) == LOAD(&g_txt_w)
        && audio_output_pending() == 0 && !g_rendering;
}

void core1_speech_poll(uint32_t now_ms)
{
    parser_apply_flush();

    /* 1. serial bytes from Core 0 */
    for (;;) {
        uint32_t r = g_txt_r;
        uint8_t c;
        if (r == LOAD(&g_txt_w)) break;
        c = g_txt[r];
        STORE(&g_txt_r, (r + 1) & TXT_MASK);
        g_last_byte_ms = now_ms;
        g_have_time = true;
        parse_byte(c);
        parser_apply_flush();
    }

    /* 2. a clause without a terminator: speak it once the line goes idle */
    if (g_utt_len && g_have_time && (uint32_t)(now_ms - g_last_byte_ms) >= DT_IDLE_SPEAK_MS)
        speak_clause();

    /* 3. Core 1 utterances (screen reader) */
    while (g_say_r != g_say_w) {
        unsigned i = g_say_r;
        g_say_r = (i + 1) % DT_SAY_QUEUE;
        /* the flush that accompanied this entry has already been requested; the
         * parser acknowledged it above, so render_text's check passes. */
        parser_apply_flush();
        render_text(g_say[i]);
    }

    /* 4. once, after the boot message is in the ring: the DECtalk warm-up runs
     * while the message is still playing, so every lazily allocated table exists
     * before the emulated machine's first clause.  Bytes that arrive meanwhile
     * wait in the FIFO (at most ~370 ms, once). */
    if (g_warmup_pending && g_say_r == g_say_w) {
        g_warmup_pending = false;
        engine_dectalk_warmup();
        g_dt_stats.warmups++;
    }
}
