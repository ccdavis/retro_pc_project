/*
 * main.c -- Core 1 speech coprocessor on a QEMU Cortex-M33 (mps2-an505), the same
 * CPU core as the RP2350.  Measures what the plan only estimated:
 *   - flash footprint (.text + .rodata + .data) and SRAM (.data + .bss + heap + stack)
 *     of the DECtalk ARM7 engine with the full dictionary, ESPK with its data pack,
 *     and the Core 1 speech core + screen reader;
 *   - instructions per second of synthesized audio for each engine (QEMU -icount
 *     shift=0: one instruction is one virtual nanosecond, read through SysTick at
 *     the board's 20 MHz, i.e. 50 instructions per tick), hence the CPU load at
 *     the RP2350's 150 MHz if the core retired one instruction per cycle.  Real
 *     silicon with XIP flash wait states will be slower; this is the floor.
 *   - that the flush protocol and the screen reader behave identically on ARM.
 * Output goes to the host through semihosting (stdout and WAV files in the cwd).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "doubletalk_core1.h"
#include "tdsr_hypervisor_reader.h"

extern const uint8_t espk_dat_blob[], espk_dat_end[];
void espk_data_set_blob(const void *p, long len);
extern uint32_t __etext, __data_start__, __data_end__, __bss_start__, __bss_end__, __StackLimit, __StackTop, end, __rom_start;

/* ---- SysTick as an instruction counter under -icount ---------------------- */
#define SYST_CSR (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)
#define TICK_NS 50u                                  /* 20 MHz board clock, 1 ns per instruction */
static uint64_t g_acc; static uint32_t g_last;
static void ticks_init(void) { SYST_RVR = 0xFFFFFF; SYST_CVR = 0; SYST_CSR = 5; g_last = 0; g_acc = 0; }
static uint64_t ticks(void)
{
    uint32_t cur = (0xFFFFFFu - SYST_CVR) & 0xFFFFFFu, d = (cur - g_last) & 0xFFFFFFu;
    g_acc += d; g_last = cur;
    return g_acc;
}
static uint64_t insns(void) { return ticks() * TICK_NS; }   /* with -icount shift=0 */

/* ---- audio consumer: pops into a RAM buffer, no screen reader during timing ---- */
#define CAP_MAX (12 * 11025)
static int16_t g_cap[CAP_MAX];
static uint32_t g_cap_n, g_popped;
static uint64_t g_now_us;
static int g_rate = 11025;
static uint64_t g_flush_at_us;
static uint32_t g_flushed_at;
static int g_sr_in_hook;
static uint64_t g_first_block_insns; static int g_want_first_block;
static void idle_hook(void)
{
    int16_t buf[64];
    int got = audio_output_pop_block(buf, 64);
    if (g_want_first_block && got) { g_want_first_block = 0; g_first_block_insns = insns(); }
    g_popped += (uint32_t)got;
    if (g_cap_n + 64 <= CAP_MAX) { memcpy(g_cap + g_cap_n, buf, sizeof buf); g_cap_n += 64; }
    g_now_us += 64 * 1000000ULL / (uint64_t)g_rate;
    if (g_flush_at_us && g_now_us >= g_flush_at_us) { g_flush_at_us = 0; g_flushed_at = g_popped; doubletalk_port_write_byte(0x18); }
    if (g_sr_in_hook) tdsr_service((uint32_t)(g_now_us / 1000));
}
static void run_until_idle(void)
{
    for (int guard = 0; guard < 4000000; guard++) {
        core1_speech_poll((uint32_t)(g_now_us / 1000));
        idle_hook();
        if (core1_speech_idle()) { core1_speech_poll((uint32_t)(g_now_us / 1000)); if (core1_speech_idle()) return; }
    }
    printf("  !! never idle\n");
}
static void write_wav(const char *name)
{
    FILE *f = fopen(name, "wb");
    uint32_t n = g_cap_n, bytes;
    while (n > 0 && g_cap[n - 1] == 0) n--;
    bytes = n * 2;
    if (!f) { printf("  cannot write %s\n", name); return; }
    fwrite("RIFF", 1, 4, f); { uint32_t v = 36 + bytes; fwrite(&v, 4, 1, f); }
    fwrite("WAVEfmt ", 1, 8, f); { uint32_t v = 16; fwrite(&v, 4, 1, f); }
    { uint16_t v = 1; fwrite(&v, 2, 1, f); fwrite(&v, 2, 1, f); }
    { uint32_t v = (uint32_t)g_rate; fwrite(&v, 4, 1, f); v *= 2; fwrite(&v, 4, 1, f); }
    { uint16_t v = 2; fwrite(&v, 2, 1, f); v = 16; fwrite(&v, 2, 1, f); }
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(g_cap, 2, n, f);
    fclose(f);
    printf("  wrote %s (%.2f s)\n", name, (double)n / g_rate);
}

static void stack_paint(void)
{
    uint32_t *d = &__StackLimit, *top = (uint32_t *)((uintptr_t)__builtin_frame_address(0) - 512);
    while (d < top) *d++ = 0xA5A5A5A5u;
}
static unsigned long stack_hwm(void)
{
    uint32_t *p = &__StackLimit;
    while (p < &__StackTop && *p == 0xA5A5A5A5u) p++;
    return (unsigned long)((uintptr_t)&__StackTop - (uintptr_t)p);
}

static char g_last_text[DT_SAY_MAX + 8]; static int g_nutt;
static void observer(synth_engine_id_t e, const char *t) { (void)e; strncpy(g_last_text, t, sizeof g_last_text - 1); g_nutt++; }

static const char *k_sentence = "The quick brown fox jumps over the lazy dog, and then it runs away into the forest.\r";

static void time_engine(synth_engine_id_t eng, const char *name, const char *wav)
{
    uint64_t t0, t1; double audio_s, ins, per_s, load150;
    g_dt_state.engine = (uint8_t)eng;
    g_dt_state.speed_0_9 = 5; g_dt_state.pitch_0_99 = 50; g_dt_state.volume_0_9 = 5;
    g_cap_n = 0; g_popped = 0;
    t0 = insns();
    doubletalk_write_string(k_sentence);
    run_until_idle();
    t1 = insns();
    audio_s = (double)g_popped / g_rate;
    ins = (double)(t1 - t0);
    per_s = audio_s > 0 ? ins / audio_s : 0;
    load150 = per_s / 150e6 * 100.0;
    printf("  %-8s %5.2f s audio, %7.1f M instructions, %6.2f M insn per audio second, "
           "%5.1f%% of a 150 MHz core (%.0fx real time at 1 IPC)\n",
           name, audio_s, ins / 1e6, per_s / 1e6, load150, load150 > 0 ? 100.0 / load150 : 0);
    write_wav(wav);
}

static tdsr_termcell_t g_grid[25][80];
static void grid_puts(int r, int c, const char *s) { for (; *s && c < 80; s++, c++) { g_grid[r][c].ch = *s; g_grid[r][c].fg = 7; g_grid[r][c].bg = 0; } }

int main(void)
{
    uint64_t t0, t1, init_insns = 0;
    uintptr_t heap0;
    printf("== Core 1 speech coprocessor on QEMU Cortex-M33 (mps2-an505) ==\n");
    ticks_init();
    /* calibrate: 1,000,000 iterations of a 2-instruction loop */
    { volatile uint32_t n = 1000000; t0 = ticks(); __asm volatile ("1: subs %0, %0, #1\n bne 1b" : "+r"(n)); t1 = ticks();
      printf("calibration: 2,000,000 instructions = %llu ticks (%.1f instructions per tick; expected %u at 1 ns/insn)\n",
             (unsigned long long)(t1 - t0), 2000000.0 / (double)(t1 - t0), TICK_NS); }

    printf("\n[image] flash (text+rodata+data) %lu bytes, .data %lu, .bss %lu\n",
           (unsigned long)(((uintptr_t)&__etext - (uintptr_t)&__rom_start) + ((uintptr_t)&__data_end__ - (uintptr_t)&__data_start__)),
           (unsigned long)((uintptr_t)&__data_end__ - (uintptr_t)&__data_start__),
           (unsigned long)((uintptr_t)&__bss_end__ - (uintptr_t)&__bss_start__));
    printf("        ESPK.DAT blob %ld bytes in flash\n", (long)(espk_dat_end - espk_dat_blob));
    heap0 = (uintptr_t)sbrk(0);

    core1_set_render_observer(observer);
    core1_set_idle_hook(idle_hook);
    espk_data_set_blob(espk_dat_blob, (long)(espk_dat_end - espk_dat_blob));

    printf("\n[init]\n");
    /* power-on path as the firmware runs it: init the primary engine, speak */
    stack_paint();
    t0 = insns(); core1_speech_init(SYNTH_ENGINE_DECTALK, g_rate); t1 = insns();
    init_insns = t1 - t0;
    printf("  DECtalk init (no warm-up): %.2f M instructions (%.1f ms at 150 MHz), heap %lu bytes\n",
           (double)init_insns / 1e6, (double)init_insns / 150e3, (unsigned long)((uintptr_t)sbrk(0) - heap0));
    tdsr_init();

    printf("\n[boot: chime, announcement, then the DECtalk warm-up while it plays]\n");
    g_cap_n = 0; g_popped = 0; g_want_first_block = 1; t0 = insns();
    core1_speech_announce_boot("Talking CP M");
    run_until_idle(); t1 = insns();
    printf("  first %u-sample ring fill (chime + start of speech) ready after %.2f M instructions (%.1f ms at 150 MHz; %.1f ms including init)\n",
           DT_AUDIO_RING_SAMPLES, (double)(g_first_block_insns - t0) / 1e6, (double)(g_first_block_insns - t0) / 150e3,
           (double)(g_first_block_insns - t0 + init_insns) / 150e3);
    printf("  %.2f s of audio, %.1f M instructions including the warm-up (%.0f ms at 150 MHz); warm-ups run: %u\n",
           (double)g_popped / g_rate, (double)(t1 - t0) / 1e6, (double)(t1 - t0) / 150e3, (unsigned)g_dt_stats.warmups);
    printf("  heap after boot + warm-up: %lu bytes (DECtalk's lazily allocated tables, if any)\n", (unsigned long)((uintptr_t)sbrk(0) - heap0));
    printf("  stack high-water mark after boot (DECtalk + warm-up + harness): %lu bytes\n", stack_hwm());
    write_wav("arm_boot_dectalk.wav");

    t0 = insns(); { bool ok = core1_engine_available(SYNTH_ENGINE_ESPK); t1 = insns();
      printf("\n[ESPK lazy init + warm-up] %s, %.1f M instructions (%.0f ms at 150 MHz), heap now %lu bytes\n", ok ? "ok" : "FAILED",
             (double)(t1 - t0) / 1e6, (double)(t1 - t0) / 150e3, (unsigned long)((uintptr_t)sbrk(0) - heap0)); }

    printf("\n[synthesis cost]\n");
    stack_paint();
    time_engine(SYNTH_ENGINE_DECTALK, "DECtalk", "arm_sentence_dectalk.wav");
    printf("  stack high-water mark, DECtalk sentence: %lu bytes\n", stack_hwm());
    stack_paint();
    time_engine(SYNTH_ENGINE_ESPK,    "ESPK",    "arm_sentence_espk.wav");
    printf("  stack high-water mark, ESPK sentence:    %lu bytes\n", stack_hwm());

    printf("\n[flush on ARM]\n");
    g_dt_state.engine = SYNTH_ENGINE_DECTALK; g_cap_n = 0; g_popped = 0;
    g_flush_at_us = g_now_us + 400000;
    doubletalk_write_string("This is a long sentence that should be interrupted right in the middle by a keystroke flush.\r");
    run_until_idle();
    printf("  played %u samples before ^X, %u after; aborted utterances %u -> %s\n", (unsigned)g_flushed_at, (unsigned)(g_popped - g_flushed_at),
           (unsigned)g_dt_stats.aborted, (g_popped - g_flushed_at < 256 && g_dt_stats.aborted == 1) ? "OK" : "FAIL");
    g_nutt = 0; doubletalk_write_string("Next.\r"); run_until_idle();
    printf("  next utterance after flush: %s\n", g_nutt == 1 ? "OK" : "FAIL");

    printf("\n[screen reader on ARM: CP M console]\n");
    for (int r = 0; r < 25; r++) for (int c = 0; c < 80; c++) { g_grid[r][c].ch = ' '; g_grid[r][c].fg = 7; g_grid[r][c].bg = 0; }
    grid_puts(0, 0, "A>");
    tdsr_capture_termcells(&g_grid[0][0], 25, 80, 2, 0); tdsr_diff_now((uint32_t)(g_now_us / 1000));
    g_nutt = 0;
    doubletalk_key_event('D', 0); tdsr_service((uint32_t)(g_now_us / 1000));
    grid_puts(0, 2, "D"); tdsr_capture_termcells(&g_grid[0][0], 25, 80, 3, 0); g_now_us += 30000; tdsr_diff_now((uint32_t)(g_now_us / 1000));
    run_until_idle();
    printf("  typed D: %d utterance(s), last \"%s\", echoes suppressed %u -> %s\n", g_nutt, g_last_text, (unsigned)g_sr.suppressed_echo,
           (g_nutt == 1 && !strcmp(g_last_text, "cap D") && g_sr.suppressed_echo == 1) ? "OK" : "FAIL");
    g_nutt = 0;
    grid_puts(1, 0, "A: SPEAK    COM : WS       COM"); grid_puts(2, 0, "A>");
    tdsr_capture_termcells(&g_grid[0][0], 25, 80, 2, 2); g_now_us += 30000; tdsr_diff_now((uint32_t)(g_now_us / 1000));
    run_until_idle();
    printf("  DIR output: %d utterance(s), last \"%s\" -> %s\n", g_nutt, g_last_text, (g_nutt == 2 && !strcmp(g_last_text, "A greater")) ? "OK" : "FAIL");

    printf("\n[memory]\n");
    { uintptr_t h = (uintptr_t)sbrk(0); printf("  heap used: %lu bytes\n", (unsigned long)(h - heap0)); }
    printf("  stats: utterances %u, flushes %u, aborted %u, ring underruns %u\n", (unsigned)g_dt_stats.utterances, (unsigned)g_dt_stats.flushes, (unsigned)g_dt_stats.aborted, (unsigned)g_dt_stats.ring_underruns);
    printf("\n== done ==\n");
    return 0;
}
