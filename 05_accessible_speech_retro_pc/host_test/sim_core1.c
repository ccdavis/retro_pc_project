/*
 * sim_core1.c -- host simulation of RP2350 Core 1 (speech coprocessor + hypervisor
 * screen reader) with the real speech engines, driven like the hardware would:
 *
 *   - "Core 0" is the test: it writes DoubleTalk bytes and key events.
 *   - the audio consumer is the idle hook: it pops 64-sample blocks (5.8 ms at
 *     11025 Hz), advances a simulated clock by exactly that much, records the
 *     PCM to a WAV, and runs the screen reader, just as the DMA refill and the
 *     Core 1 loop would.
 *
 * Every spoken utterance is logged through the render observer, so the tests
 * assert on text, and the WAVs in the output directory can be listened to or
 * transcribed (vintage-pc-speech/src/espeak/tools/transcribe.py).
 *
 *   sim_core1 <outdir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include "doubletalk_core1.h"
#include "tdsr_hypervisor_reader.h"

static const char *g_outdir = ".";
static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- simulated clock, audio consumer, WAV capture ---------------------- */
static int      g_rate = 11025;
static uint64_t g_now_us = 0;
static uint32_t now_ms(void) { return (uint32_t)(g_now_us / 1000); }

static int16_t *g_wav; static size_t g_wav_n, g_wav_cap; static int g_capturing;
static uint64_t g_popped_total = 0;
static int g_hook_runs_sr = 0;
static int g_hook_calls = 0;
static uint64_t g_flush_at_us = 0;       /* "Core 0 sends ^X" when the clock passes this */
static uint64_t g_flushed_at_samples = 0;

static void wav_begin(void) { g_wav_n = 0; g_capturing = 1; }
static double wav_end(const char *name)
{
    char path[512]; FILE *f; double rms = 0; size_t i;
    uint32_t n = (uint32_t)g_wav_n, bytes = n * 2;
    /* trim trailing silence */
    while (g_wav_n > 0 && g_wav[g_wav_n - 1] == 0) g_wav_n--;
    n = (uint32_t)g_wav_n; bytes = n * 2;
    g_capturing = 0;
    snprintf(path, sizeof path, "%s/%s", g_outdir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); return 0; }
    for (i = 0; i < g_wav_n; i++) rms += (double)g_wav[i] * g_wav[i];
    rms = g_wav_n ? sqrt(rms / (double)g_wav_n) : 0;
    fwrite("RIFF", 1, 4, f);
    { uint32_t v = 36 + bytes; fwrite(&v, 4, 1, f); }
    fwrite("WAVEfmt ", 1, 8, f);
    { uint32_t v = 16; fwrite(&v, 4, 1, f); }
    { uint16_t v = 1; fwrite(&v, 2, 1, f); fwrite(&v, 2, 1, f); }
    { uint32_t v = (uint32_t)g_rate; fwrite(&v, 4, 1, f); v = (uint32_t)g_rate * 2; fwrite(&v, 4, 1, f); }
    { uint16_t v = 2; fwrite(&v, 2, 1, f); v = 16; fwrite(&v, 2, 1, f); }
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(g_wav, 2, g_wav_n, f);
    fclose(f);
    printf("  wav %-22s %6.2f s  rms %6.0f\n", name, (double)g_wav_n / g_rate, rms);
    return rms;
}

static void idle_hook(void)
{
    int16_t buf[64];
    int got = audio_output_pop_block(buf, 64);
    g_hook_calls++;
    g_popped_total += (uint64_t)got;
    if (g_capturing) {
        if (g_wav_n + 64 > g_wav_cap) { g_wav_cap = g_wav_cap ? g_wav_cap * 2 : 1 << 20; g_wav = realloc(g_wav, g_wav_cap * 2); }
        memcpy(g_wav + g_wav_n, buf, sizeof buf);
        g_wav_n += 64;
    }
    g_now_us += 64 * 1000000ULL / (uint64_t)g_rate;
    if (g_flush_at_us && g_now_us >= g_flush_at_us) {
        g_flush_at_us = 0;
        g_flushed_at_samples = g_popped_total;
        doubletalk_port_write_byte(0x18);          /* the keystroke flush from Core 0 */
        {
            int16_t probe[64];
            int got2 = audio_output_pop_block(probe, 64);
            CHECK(got2 == 0, "ring silent on the first pop after ^X (got %d samples)", got2);
        }
    }
    if (g_hook_runs_sr) tdsr_service(now_ms());
}

/* run Core 1 until nothing is queued, parsed or playing */
static void run_until_idle(void)
{
    for (int guard = 0; guard < 4000000; guard++) {
        core1_speech_poll(now_ms());
        idle_hook();
        if (core1_speech_idle()) {
            core1_speech_poll(now_ms());             /* give the idle-speak rule its chance */
            if (core1_speech_idle()) return;
        }
    }
    CHECK(0, "run_until_idle: Core 1 never went idle");
}

static void advance_ms(int ms)
{
    int blocks = (int)((int64_t)ms * g_rate / 1000 / 64) + 1;
    while (blocks-- > 0) { core1_speech_poll(now_ms()); idle_hook(); }
}

/* ---- spoken text log ---------------------------------------------------- */
#define LOG_MAX 64
static char g_log[LOG_MAX][DT_SAY_MAX + 64];
static synth_engine_id_t g_log_eng[LOG_MAX];
static int g_log_n;
static void observer(synth_engine_id_t e, const char *text)
{
    if (g_log_n < LOG_MAX) { strncpy(g_log[g_log_n], text, sizeof g_log[0] - 1); g_log_eng[g_log_n] = e; g_log_n++; }
}
static void log_clear(void) { g_log_n = 0; }
static int log_contains(const char *needle)
{
    for (int i = 0; i < g_log_n; i++) if (strstr(g_log[i], needle)) return 1;
    return 0;
}
static void log_dump(const char *title)
{
    printf("  %s: %d utterance(s)\n", title, g_log_n);
    for (int i = 0; i < g_log_n; i++) printf("    [%d] %s\n", g_log_eng[i], g_log[i]);
}

/* ---- hang guard --------------------------------------------------------- */
static void on_alarm(int sig) { (void)sig; printf("  FAIL: timeout (engine hang?)\n"); _exit(2); }

/* ---- screen helpers (CP/M VT100 card grid) ------------------------------ */
static tdsr_termcell_t g_grid[25][80];
static int g_cx, g_cy;
static void scr_clear(void) { for (int r = 0; r < 25; r++) for (int c = 0; c < 80; c++) { g_grid[r][c].ch = ' '; g_grid[r][c].fg = 7; g_grid[r][c].bg = 0; } g_cx = g_cy = 0; }
static void scr_puts(int r, int c, const char *s, int bg)
{
    for (; *s && c < 80; s++, c++) { g_grid[r][c].ch = *s; g_grid[r][c].bg = (uint8_t)bg; g_grid[r][c].fg = bg ? 0 : 7; }
    g_cx = c; g_cy = r;
}
static void scr_scroll(void)
{
    memmove(&g_grid[0][0], &g_grid[1][0], sizeof(g_grid[0]) * 24);
    for (int c = 0; c < 80; c++) { g_grid[24][c].ch = ' '; g_grid[24][c].bg = 0; g_grid[24][c].fg = 7; }
}
static void scr_capture(void) { tdsr_capture_termcells(&g_grid[0][0], 25, 80, g_cx, g_cy); }
static void scr_tick(void)
{
    /* one 30 ms screen-reader tick followed by rendering whatever it said */
    scr_capture();
    g_now_us += 30000;
    tdsr_diff_now(now_ms());
    run_until_idle();
}

static void key_nowait(uint8_t ascii, uint8_t mods)
{
    doubletalk_key_event(ascii, mods);
    tdsr_service(now_ms());
}

static void key(uint8_t ascii, uint8_t mods)
{
    /* Core 0 delivers the key; Core 1 picks it up in its next service call */
    doubletalk_key_event(ascii, mods);
    tdsr_service(now_ms());
    run_until_idle();
}

/* ======================================================================== */
/* Tests                                                                     */
/* ======================================================================== */
static void test_boot(synth_engine_id_t eng, const char *wavname)
{
    double rms;
    printf("\n[boot: %s]\n", wavname);
    log_clear();
    g_dt_state.engine = (uint8_t)eng;
    wav_begin();
    core1_speech_announce_boot("Talking CP M");
    run_until_idle();
    rms = wav_end(wavname);
    CHECK(g_log_n == 2 && !strcmp(g_log[0], DT_CHIME_TEXT), "chime first, then the message (got %d)", g_log_n);
    CHECK(log_contains("ready") && log_contains("Alt H"), "boot text");
    {   /* the chime is audible at once: the first 50 ms of the capture are not silent */
        double e = 0; size_t n = (size_t)g_rate / 20; if (n > g_wav_n) n = g_wav_n;
        for (size_t i = 0; i < n; i++) e += (double)g_wav[i] * g_wav[i];
        CHECK(n && sqrt(e / n) > 1000, "chime audible in the first 50 ms (rms %.0f)", n ? sqrt(e / n) : 0);
    }
    if (eng == SYNTH_ENGINE_DECTALK) CHECK(g_dt_stats.warmups == 1, "DECtalk warm-up ran once after the boot message (%u)", g_dt_stats.warmups);
    CHECK(g_wav_n > (size_t)g_rate, "boot audio longer than 1 s (%zu samples)", g_wav_n);
    CHECK(rms > 200, "boot audio not silent (rms %.0f)", rms);
    CHECK(g_dt_stats.ring_underruns == 0 || eng == SYNTH_ENGINE_TONE, "no ring underruns while rendering (%u)", g_dt_stats.ring_underruns);
}

static void test_sentence(synth_engine_id_t eng, const char *wavname)
{
    printf("\n[sentence: %s]\n", wavname);
    log_clear();
    g_dt_state.engine = (uint8_t)eng;
    g_dt_state.speed_0_9 = 5; g_dt_state.pitch_0_99 = 50; g_dt_state.volume_0_9 = 5;
    wav_begin();
    doubletalk_write_string("The quick brown fox jumps over the lazy dog, and then it runs away into the forest.\r");
    run_until_idle();
    wav_end(wavname);
    CHECK(g_log_n == 1 && strstr(g_log[0], "lazy dog"), "sentence spoken as one clause");
}

static void test_commands(void)
{
    printf("\n[DoubleTalk command parsing]\n");
    log_clear();
    doubletalk_write_string("\x01" "7S" "\x01" "60P" "\x01" "1O" "\x01" "9V" "Hello from Provox\r");
    run_until_idle();
    CHECK(g_dt_state.speed_0_9 == 7, "speed 7 (got %d)", g_dt_state.speed_0_9);
    CHECK(g_dt_state.pitch_0_99 == 60, "pitch 60 (got %d)", g_dt_state.pitch_0_99);
    CHECK(g_dt_state.voice_0_7 == 1, "voice 1 (got %d)", g_dt_state.voice_0_7);
    CHECK(g_dt_state.volume_0_9 == 9, "volume 9 (got %d)", g_dt_state.volume_0_9);
    CHECK(g_log_n == 1 && strcmp(g_log[0], "Hello from Provox") == 0, "text without command bytes (%s)", g_log_n ? g_log[0] : "-");

    /* ^A^A restarts the command; a letter with no number uses 0 */
    log_clear();
    doubletalk_write_string("\x01" "4" "\x01" "3S" "x\r");
    run_until_idle();
    CHECK(g_dt_state.speed_0_9 == 3, "^A^A restart -> speed 3 (got %d)", g_dt_state.speed_0_9);

    /* ^A@ reset; unknown command letters are ignored, not spoken */
    log_clear();
    doubletalk_write_string("\x01" "@" "\x01" "2T" "\x01" "F" "ok\r");
    run_until_idle();
    CHECK(g_dt_state.speed_0_9 == 5 && g_dt_state.pitch_0_99 == 50 && g_dt_state.volume_0_9 == 5, "^A@ reset");
    CHECK(g_log_n == 1 && strcmp(g_log[0], "ok") == 0, "ignored commands leave no residue (%s)", g_log_n ? g_log[0] : "-");

    /* a clause with no terminator is spoken when the line goes idle */
    log_clear();
    doubletalk_write_string("No terminator here");
    advance_ms(10);
    CHECK(g_log_n == 0, "not spoken yet after 10 ms");
    advance_ms(DT_IDLE_SPEAK_MS + 40);
    run_until_idle();
    CHECK(g_log_n == 1 && strcmp(g_log[0], "No terminator here") == 0, "idle clause spoken (%d)", g_log_n);

    /* 200-byte buffer: split at the last space, the word in progress is kept */
    log_clear();
    {
        char big[400]; int i;
        for (i = 0; i < 300; i++) big[i] = (i % 10 == 9) ? ' ' : (char)('a' + i % 26);
        big[300] = '\r'; big[301] = 0;
        doubletalk_write_string(big);
    }
    run_until_idle();
    CHECK(g_log_n == 2, "long input split into 2 clauses (got %d)", g_log_n);
    CHECK(g_log_n == 2 && strlen(g_log[0]) <= DT_UTT_MAX && g_log[0][strlen(g_log[0]) - 1] == ' ', "first clause ends at a word boundary");
}

static void test_flush(synth_engine_id_t eng, const char *wavname)
{
    uint64_t before;
    printf("\n[flush mid-utterance: %s]\n", wavname);
    log_clear();
    g_dt_state.engine = (uint8_t)eng;
    g_dt_state.speed_0_9 = 5;
    wav_begin();
    before = g_popped_total;
    g_flush_at_us = g_now_us + 400000;             /* Core 0 presses a key 400 ms into playback */
    doubletalk_write_string("This is a long sentence that should be interrupted right in the middle by a keystroke flush from the screen reader.\r");
    run_until_idle();
    wav_end(wavname);
    CHECK(g_flush_at_us == 0, "the flush was injected");
    CHECK(g_dt_stats.aborted >= 1, "utterance counted as aborted");
    CHECK(g_flushed_at_samples - before >= (uint64_t)(g_rate * 35 / 100) && g_flushed_at_samples - before <= (uint64_t)(g_rate * 45 / 100),
          "about 400 ms played before the flush (%llu samples)", (unsigned long long)(g_flushed_at_samples - before));
    CHECK(g_popped_total - g_flushed_at_samples < 64 * 4, "almost nothing played after the flush (%llu samples)", (unsigned long long)(g_popped_total - g_flushed_at_samples));
    CHECK(g_wav_n < (size_t)g_rate, "WAV cut short (%zu samples)", g_wav_n);

    /* regression: the next utterance must render (DECtalk hung here when the callback returned NULL) */
    log_clear();
    before = g_popped_total;
    alarm(20);
    doubletalk_write_string("Next sentence after the flush.\r");
    run_until_idle();
    alarm(0);
    CHECK(g_log_n == 1 && g_popped_total > before + (uint64_t)g_rate / 2, "speech resumes after a flush (%d utt, %llu samples)", g_log_n, (unsigned long long)(g_popped_total - before));

    /* ^X with text queued behind it: the queued text is dropped, text after ^X is spoken */
    log_clear();
    doubletalk_write_string("Stale text that was queued\r");
    doubletalk_port_write_byte(0x18);
    doubletalk_write_string("Fresh text\r");
    run_until_idle();
    CHECK(g_log_n == 1 && strcmp(g_log[0], "Fresh text") == 0, "queued text before ^X dropped (%d: %s)", g_log_n, g_log_n ? g_log[0] : "-");
}

static void test_say_queue(void)
{
    printf("\n[Core 1 say queue]\n");
    log_clear();
    core1_say("Alpha", true);
    core1_say("Bravo", false);
    run_until_idle();
    CHECK(g_log_n == 2 && !strcmp(g_log[0], "Alpha") && !strcmp(g_log[1], "Bravo"), "two queued utterances in order (%d)", g_log_n);
    log_clear();
    core1_say("Charlie", false);
    core1_say("Delta", true);          /* flush drops Charlie */
    run_until_idle();
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "Delta"), "flush_first drops what was queued (%d: %s)", g_log_n, g_log_n ? g_log[0] : "-");
}

static void test_uart(void)
{
    printf("\n[16550 register view]\n");
    CHECK(doubletalk_port_status() == 0x60B0, "INT 14h status 0x60B0");
    CHECK(doubletalk_uart_read(5) == 0x60, "LSR THRE|TEMT");
    CHECK(doubletalk_uart_read(6) == 0xB0, "MSR DCD|DSR|CTS");
    CHECK(doubletalk_uart_read(2) == 0x01, "IIR no interrupt");
    log_clear();
    doubletalk_uart_write(3, 0x80);             /* DLAB on: divisor writes must not become text */
    doubletalk_uart_write(0, 0x0C);
    doubletalk_uart_write(3, 0x03);
    doubletalk_uart_write(0, 'H'); doubletalk_uart_write(0, 'i'); doubletalk_uart_write(0, '\r');
    run_until_idle();
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "Hi"), "THR bytes reach the synthesizer, divisor latch does not (%s)", g_log_n ? g_log[0] : "-");
}

static void test_fifo_overrun(void)
{
    printf("\n[text FIFO overrun never blocks Core 0]\n");
    uint32_t before = g_dt_stats.text_overruns;
    for (int i = 0; i < DT_TEXT_FIFO_SIZE + 100; i++) doubletalk_port_write_byte('a');
    CHECK(g_dt_stats.text_overruns > before, "overrun counted (%u)", g_dt_stats.text_overruns - before);
    doubletalk_port_write_byte(0x18);           /* throw it all away */
    run_until_idle();
}

static void test_apple2(void)
{
    static uint8_t main_ram[65536], aux_ram[65536];
    printf("\n[Apple //e text page decoding]\n");
    memset(main_ram, 0, sizeof main_ram); memset(aux_ram, 0, sizeof aux_ram);
    memset(&main_ram[0x400], 0xA0, 0x400); memset(&aux_ram[0x400], 0xA0, 0x400);
    /* 80 columns: even columns in aux, odd in main; inverse "FILE" at row 0 */
    aux_ram[0x400] = 'F' & 0x3F; main_ram[0x400] = 'I' & 0x3F; aux_ram[0x401] = 'L' & 0x3F; main_ram[0x401] = 'E' & 0x3F;
    /* row 1 (base 0x480): normal "Hi" with lower case, flashing cursor (flashing space = 0x60) after it */
    aux_ram[0x480] = 0xC8; main_ram[0x480] = 0xE9; aux_ram[0x481] = 0x60;
    /* row 2 (0x500): flashing '?' is 0x7F; a flashing 'A' is 0x41 */
    aux_ram[0x500] = 0x7F; main_ram[0x500] = 0x41;
    /* row 8 (0x428) in 40-column memory: inverse "]" prompt would be 0x1D; normal '@' 0x80 */
    main_ram[0x428] = 0x1D; main_ram[0x429] = 0x80;
    tdsr_capture_apple2(main_ram, aux_ram, true, false, 4, 1);
    CHECK(memcmp(g_sr.curr_ch[0], "FILE", 4) == 0 && (g_sr.curr_attr[0][0] & SR_ATTR_INVERSE), "80-col interleave + inverse");
    CHECK(g_sr.curr_ch[1][0] == 'H' && g_sr.curr_ch[1][1] == 'i', "normal upper and lower case (%c%c)", g_sr.curr_ch[1][0], g_sr.curr_ch[1][1]);
    CHECK(g_sr.curr_ch[1][2] == ' ' && (g_sr.curr_attr[1][2] & SR_ATTR_BLINK), "flashing space is a space, not a grave accent (got '%c')", g_sr.curr_ch[1][2]);
    CHECK(g_sr.curr_ch[2][0] == '?' && g_sr.curr_ch[2][1] == 'A', "flashing '?' and 'A' (%c %c)", g_sr.curr_ch[2][0], g_sr.curr_ch[2][1]);
    CHECK(g_sr.rows == 24 && g_sr.cols == 80 && g_sr.hw_x == 4 && g_sr.hw_y == 1, "geometry and cursor");
    /* 40 columns */
    tdsr_capture_apple2(main_ram, aux_ram, false, false, 0, 8);
    CHECK(g_sr.curr_ch[8][0] == ']' && (g_sr.curr_attr[8][0] & SR_ATTR_INVERSE) && g_sr.curr_ch[8][1] == '@', "40-col inverse ']' and normal '@' (%c %c)", g_sr.curr_ch[8][0], g_sr.curr_ch[8][1]);
    CHECK(g_sr.cols == 40 && g_sr.curr_ch[0][40] == ' ', "40-col mode blanks columns 40..79");
    /* ALTCHARSET: 0x41 is a MouseText glyph (decoration), 0x61 inverse lower case 'a' */
    aux_ram[0x500] = 0x41; main_ram[0x500] = 0x61;
    tdsr_capture_apple2(main_ram, aux_ram, true, true, 0, 0);
    CHECK(g_sr.curr_ch[2][0] == ' ' && g_sr.curr_ch[2][1] == 'a' && (g_sr.curr_attr[2][1] & SR_ATTR_INVERSE), "ALTCHARSET MouseText / inverse lower case (%c %c)", g_sr.curr_ch[2][0], g_sr.curr_ch[2][1]);
}

static void test_cga(void)
{
    static uint8_t vram[80 * 25 * 2];
    printf("\n[CGA text page highlight detection]\n");
    for (int i = 0; i < 80 * 25; i++) { vram[i * 2] = ' '; vram[i * 2 + 1] = 0x07; }
    /* row 3: plain text with one word in white-on-blue (0x1F): a highlight */
    { const char *t = "File  Edit  View"; for (int i = 0; t[i]; i++) { vram[(3 * 80 + i) * 2] = (uint8_t)t[i]; vram[(3 * 80 + i) * 2 + 1] = (i >= 6 && i < 10) ? 0x1F : 0x07; } }
    /* row 0: a full inverse menu bar (0x70) with "Edit" selected in 0x0F on blue? keep it: selected item 0x1F */
    { const char *t = " File  Edit  View  Help "; for (int i = 0; i < 80; i++) { vram[(0 * 80 + i) * 2] = (uint8_t)(i < 24 ? t[i] : ' '); vram[(0 * 80 + i) * 2 + 1] = 0x70; } for (int i = 7; i < 11; i++) vram[(0 * 80 + i) * 2 + 1] = 0x1F; }
    /* row 10: blinking text (bit 7) on a plain row is not a highlight */
    vram[(10 * 80 + 5) * 2] = 'X'; vram[(10 * 80 + 5) * 2 + 1] = 0x87;
    tdsr_capture_cga(vram, 0, 24);
    CHECK((g_sr.curr_attr[3][6] & SR_ATTR_INVERSE) && !(g_sr.curr_attr[3][0] & SR_ATTR_INVERSE), "white-on-blue word is the highlight on a grey row");
    CHECK((g_sr.curr_attr[0][7] & SR_ATTR_INVERSE) && !(g_sr.curr_attr[0][1] & SR_ATTR_INVERSE), "selected item inside a full inverse bar");
    CHECK(!(g_sr.curr_attr[10][5] & SR_ATTR_INVERSE), "blink bit alone is not a highlight");
}

static void test_console_scenario(void)
{
    printf("\n[CP/M console: echo suppression, output, scroll]\n");
    tdsr_init();
    scr_clear();
    scr_puts(0, 0, "A>", 0);
    scr_capture();
    tdsr_diff_now(now_ms());               /* baseline */
    run_until_idle();

    /* type D I R: each key is echoed once by the key handler; the screen echo must be silent */
    log_clear();
    /* the emulator echoes the key within a millisecond, long before the spoken echo ends */
    key_nowait('D', 0); scr_puts(0, 2, "D", 0); scr_tick();
    key_nowait('I', 0); scr_puts(0, 3, "I", 0); scr_tick();
    key_nowait('R', 0); scr_puts(0, 4, "R", 0); scr_tick();
    log_dump("typing DIR");
    CHECK(g_log_n == 3 && !strcmp(g_log[0], "cap D") && !strcmp(g_log[1], "cap I") && !strcmp(g_log[2], "cap R"), "three key echoes, no line re-read (%d utterances)", g_log_n);
    CHECK(g_sr.suppressed_echo == 3, "3 screen echoes suppressed (got %u)", g_sr.suppressed_echo);

    /* Enter: four lines of output and a new prompt appear between the old and new cursor rows */
    log_clear();
    key('\r', 0);
    scr_puts(1, 0, "A: SPEAK    COM : WS       COM : SC       COM : MBASIC   COM", 0);
    scr_puts(2, 0, "A: PIP      COM : STAT     COM : SUBMIT   COM : ED       COM", 0);
    scr_puts(3, 0, "A: TURBO    COM : TURBO    OVR : TURBO    MSG", 0);
    scr_puts(4, 0, "A>", 0);
    scr_tick();
    log_dump("after DIR");
    CHECK(g_log_n == 4, "three output lines and the prompt spoken (%d)", g_log_n);
    CHECK(g_log_n >= 1 && strstr(g_log[0], "SPEAK") && strstr(g_log[0], "COM"), "first output line text");
    CHECK(g_log_n == 4 && !strcmp(g_log[3], "A greater"), "prompt spoken with its symbol (%s)", g_log_n == 4 ? g_log[3] : "-");

    /* fill the screen, then scroll by one: only the new bottom line is spoken */
    for (int r = 5; r < 25; r++) { char line[32]; snprintf(line, sizeof line, "line %d of the file", r); scr_puts(r, 0, line, 0); }
    g_cx = 0; g_cy = 24;
    scr_tick();
    log_clear();
    scr_scroll();
    scr_puts(24, 0, "the new bottom line", 0);
    scr_tick();
    log_dump("after scroll");
    CHECK(g_sr.scrolls == 1, "scroll detected (%u)", g_sr.scrolls);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "the new bottom line"), "only the new line spoken (%d)", g_log_n);

    /* a full repaint with the cursor parked: silent, review cursor at the hw cursor */
    log_clear();
    scr_clear();
    for (int r = 0; r < 20; r++) scr_puts(r, 0, "WordStar document text everywhere on the screen", 0);
    g_cx = 0; g_cy = 0;
    scr_tick();
    CHECK(g_log_n == 0 && g_sr.repaints == 1, "repaint silent (%d utterances, %u repaints)", g_log_n, g_sr.repaints);
    CHECK(g_sr.rev_x == 0 && g_sr.rev_y == 0, "review cursor snapped");

    /* cursor down one row, nothing else: the new line is read */
    log_clear();
    g_cy = 1;
    scr_tick();
    CHECK(g_log_n == 1 && strstr(g_log[0], "WordStar document"), "cursor row change reads the line (%d)", g_log_n);

    /* cursor right (no key typed recently): the character under it */
    log_clear();
    g_now_us += 1000000;                   /* well past the echo window */
    g_cx = 1;
    scr_tick();
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "o"), "cursor column change reads the character (%s)", g_log_n ? g_log[0] : "-");

    /* backspace says the character being deleted */
    log_clear();
    g_cx = 4;
    scr_tick(); log_clear();
    key(0x08, 0);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "d"), "backspace speaks the deleted character (%s)", g_log_n ? g_log[0] : "-");
}

static void test_highlight_bar(void)
{
    printf("\n[menu bar highlight tracking]\n");
    tdsr_init();
    scr_clear();
    scr_puts(0, 0, " File ", 0); scr_puts(0, 6, " Edit ", 7); scr_puts(0, 12, " View ", 0);
    g_cx = 0; g_cy = 24;
    scr_capture(); tdsr_diff_now(now_ms()); run_until_idle();
    log_clear();
    scr_puts(0, 6, " Edit ", 0); scr_puts(0, 12, " View ", 7);
    g_cx = 0; g_cy = 24;
    scr_tick();
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "View"), "moved highlight spoken (%d: %s)", g_log_n, g_log_n ? g_log[0] : "-");
    /* SuperCalc style: a cell highlight appears while a status line also changes: highlight wins */
    log_clear();
    scr_puts(0, 12, " View ", 0);
    scr_puts(5, 10, " 1234.50 ", 7);
    scr_puts(24, 0, "Width: 9  Memory: 212  Last Col/Row: H20", 0);
    g_cx = 0; g_cy = 24;
    scr_tick();
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "1234.50"), "cell highlight spoken, status line not (%d: %s)", g_log_n, g_log_n ? g_log[0] : "-");
}

static void test_review_keys(void)
{
    printf("\n[review cursor keys]\n");
    tdsr_init();
    scr_clear();
    scr_puts(0, 0, "A>DIR *.COM", 0);
    scr_puts(1, 0, "Hello World! x=42", 0);
    g_cx = 0; g_cy = 2;
    scr_capture(); tdsr_diff_now(now_ms()); run_until_idle();
    g_sr.rev_x = 0; g_sr.rev_y = 1;

    log_clear(); key('i', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "Hello World! x equals 42"), "Alt+I current line, some punctuation (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key_nowait('i', DT_MOD_ALT); key_nowait('i', DT_MOD_ALT); run_until_idle();   /* double tap */
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "Hello World bang x equals 42"), "Alt+I twice: all punctuation (%s)", g_log_n ? g_log[0] : "-");
    g_now_us += 1000000;
    log_clear(); key('k', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "Hello"), "Alt+K current word (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key_nowait('k', DT_MOD_ALT); key_nowait('k', DT_MOD_ALT); run_until_idle();
    CHECK(g_log_n == 1 && !strncmp(g_log[0], "cap H, e, l, l, o, ", 19), "Alt+K twice spells (%s)", g_log_n ? g_log[0] : "-");
    g_now_us += 1000000;
    log_clear(); key('l', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "World!"), "Alt+L next word, '!' is not in the 'some' set (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key(',', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "cap W"), "Alt+, current char (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key_nowait(',', DT_MOD_ALT); key_nowait(',', DT_MOD_ALT); run_until_idle();
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "cap Whiskey"), "Alt+, twice: phonetic (%s)", g_log_n ? g_log[0] : "-");
    g_now_us += 1000000;
    log_clear(); key('u', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "A greater DIR star .COM"), "Alt+U previous line (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key('u', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "top"), "Alt+U at the top (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key('o', DT_MOD_ALT | DT_MOD_SHIFT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "blank") && g_sr.rev_y == 24, "Alt+Shift+O bottom (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key('c', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "Row 3, column 1"), "Alt+C cursor position (%s)", g_log_n ? g_log[0] : "-");
    log_clear(); key(0x08, DT_MOD_ALT | DT_MOD_SHIFT);
    CHECK(g_log_n == 1 && strstr(g_log[0], "Speech reset. Rate 200"), "panic chord (%s)", g_log_n ? g_log[0] : "-");
    CHECK(doubletalk_key_event('i', DT_MOD_ALT) == 1, "Alt+I reported consumed to Core 0");
    CHECK(doubletalk_key_event('i', 0) == 0, "plain i passed through");
    run_until_idle();
    /* repeated symbols condensed */
    log_clear();
    scr_puts(3, 0, "=========== Title ===========", 0);
    scr_capture();
    g_sr.rev_y = 3; g_sr.rev_x = 0;
    key('i', DT_MOD_ALT);
    CHECK(g_log_n == 1 && !strcmp(g_log[0], "11 equals Title 11 equals"), "runs condensed (%s)", g_log_n ? g_log[0] : "-");
}

static void test_engine_switch(void)
{
    printf("\n[engine switching]\n");
    log_clear();
    doubletalk_write_string("\x01" "2Z" "tone test\r");
    run_until_idle();
    CHECK(g_dt_state.engine == SYNTH_ENGINE_TONE, "^A2Z selects the tone voice (%d)", g_dt_state.engine);
    CHECK(g_log_n == 1 && g_log_eng[0] == SYNTH_ENGINE_TONE, "rendered with the tone voice");
    wav_begin();
    doubletalk_write_string("Tone voice test\r");
    run_until_idle();
    wav_end("tone_voice.wav");
    doubletalk_write_string("\x01" "0Z" "\r");
    run_until_idle();
    CHECK(g_dt_state.engine == (core1_engine_available(SYNTH_ENGINE_DECTALK) ? SYNTH_ENGINE_DECTALK : SYNTH_ENGINE_TONE), "^A0Z back to DECtalk when available");
    log_clear();
    key('s', DT_MOD_ALT);
    CHECK(g_log_n == 1 && strstr(g_log[0], "voice"), "Alt+S announces the next voice (%s)", g_log_n ? g_log[0] : "-");
    g_dt_state.engine = core1_engine_available(SYNTH_ENGINE_DECTALK) ? SYNTH_ENGINE_DECTALK : SYNTH_ENGINE_TONE;
}

static void test_fallback(void)
{
    printf("\n[engine fallback]\n");
    if (!core1_engine_available(SYNTH_ENGINE_DECTALK)) { printf("  (no DECtalk: skipped)\n"); return; }
    g_dt_state.engine = SYNTH_ENGINE_DECTALK;
    log_clear();
    core1_mark_engine_failed(SYNTH_ENGINE_DECTALK);          /* as the watchdog path would */
    doubletalk_write_string("Fallback test\r");
    run_until_idle();
    CHECK(g_log_n == 1 && g_log_eng[0] != SYNTH_ENGINE_DECTALK, "spoken by the next engine (%d)", g_log_n ? (int)g_log_eng[0] : -1);
    CHECK(g_dt_state.engine != SYNTH_ENGINE_DECTALK && g_dt_stats.engine_failures >= 1, "engine marked failed and deselected");
    /* re-initialise: the engines come back (keep the session statistics for the summary) */
    { dt_stats_t saved = g_dt_stats; core1_speech_init(SYNTH_ENGINE_DECTALK, g_rate); g_dt_stats = saved; }
    CHECK(g_dt_state.engine == SYNTH_ENGINE_DECTALK, "re-init restores DECtalk");
    tdsr_init();
}

static void test_provox_mode(void)
{
    printf("\n[Provox owns COM2: hypervisor reader defers]\n");
    tdsr_init();
    g_sr.com2_provox_active = true;
    scr_clear(); scr_puts(0, 0, "C:\\>", 0); scr_capture(); tdsr_diff_now(now_ms());
    log_clear();
    scr_puts(1, 0, "Volume in drive C has no label", 0); g_cy = 2; g_cx = 0;
    scr_tick();
    CHECK(g_log_n == 0, "silent while Provox drives the port (%d)", g_log_n);
    g_sr.com2_provox_active = false;
}

int main(int argc, char **argv)
{
    synth_engine_id_t primary;
    if (argc > 1) g_outdir = argv[1];
    signal(SIGALRM, on_alarm);
    setvbuf(stdout, NULL, _IONBF, 0);

    core1_set_render_observer(observer);
    core1_set_idle_hook(idle_hook);
    printf("initialising engines...\n");
    alarm(60);
    core1_speech_init(SYNTH_ENGINE_DECTALK, g_rate);
    alarm(0);
    primary = (synth_engine_id_t)g_dt_state.engine;
    printf("engines: DECtalk %s, ESPK %s, tone yes; primary %d\n",
           core1_engine_available(SYNTH_ENGINE_DECTALK) ? "yes" : "no",
           core1_engine_available(SYNTH_ENGINE_ESPK) ? "yes" : "no", primary);
    tdsr_init();

    if (core1_engine_available(SYNTH_ENGINE_DECTALK)) {
        test_boot(SYNTH_ENGINE_DECTALK, "boot_dectalk.wav");
        test_sentence(SYNTH_ENGINE_DECTALK, "sentence_dectalk.wav");
        test_flush(SYNTH_ENGINE_DECTALK, "flush_dectalk.wav");
    }
    if (core1_engine_available(SYNTH_ENGINE_ESPK)) {
        test_boot(SYNTH_ENGINE_ESPK, "boot_espk.wav");
        test_sentence(SYNTH_ENGINE_ESPK, "sentence_espk.wav");
        test_flush(SYNTH_ENGINE_ESPK, "flush_espk.wav");
    }
    g_dt_state.engine = (uint8_t)primary;
    test_boot(SYNTH_ENGINE_TONE, "boot_tone.wav");
    g_dt_state.engine = (uint8_t)primary;

    test_commands();
    test_say_queue();
    test_uart();
    test_fifo_overrun();
    test_apple2();
    test_cga();
    test_console_scenario();
    test_highlight_bar();
    test_review_keys();
    test_engine_switch();
    test_fallback();
    test_provox_mode();

    printf("\nstats: utterances %u, flushes %u, aborted %u, text overruns %u, ring underruns %u, samples out %u, failures %u, fallbacks %u, warmups %u\n",
           g_dt_stats.utterances, g_dt_stats.flushes, g_dt_stats.aborted, g_dt_stats.text_overruns,
           g_dt_stats.ring_underruns, g_dt_stats.samples_out, g_dt_stats.engine_failures, g_dt_stats.fallbacks, g_dt_stats.warmups);
    printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
