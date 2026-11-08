/*
 * main.c -- Waveshare RP2350-Zero firmware skeleton: Core 1 speech coprocessor
 * (DECtalk / ESPK) + hypervisor screen reader, PWM headphone audio on GPIO 20/21.
 *
 * Core 0 here is a stand-in for the emulated machine: it keeps an 80x25 VT100
 * cell grid (the same TermCell layout the HDMI card uses), prints a prompt,
 * "types" a DIR command every few seconds, and forwards console keys from the
 * UART (ESC + letter = Alt + letter) through doubletalk_key_event().  Replace it
 * with the Z80 / 65C02 / 8088 core; the Core 1 side does not change.
 *
 * Audio: an 11025 Hz repeating timer on Core 1's own alarm pool pops one sample
 * and loads two PWM slices (GPIO 20 = slice 2 A, GPIO 21 = slice 2 B) running
 * at ~73 kHz with 11-bit resolution; an RC filter (220 ohm + 100 nF) feeds the
 * 3.5 mm jack.  HDMI data-island audio is future work.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "doubletalk_core1.h"
#include "tdsr_hypervisor_reader.h"

#define AUDIO_RATE   11025
#define PWM_L_PIN    20
#define PWM_R_PIN    21
#define PWM_WRAP     2047          /* 11 bits: 150 MHz / 2048 = 73 kHz carrier */

extern const uint8_t espk_dat_blob[], espk_dat_end[];
void espk_data_set_blob(const void *p, long len);

/* The SDK's default Core 1 stack is 2 KB.  DECtalk's text pipeline and ESPK's
 * translator need far more (measured on QEMU: see arm_qemu), so Core 1 gets its own. */
#define CORE1_STACK_WORDS (64 * 1024 / 4)
static uint32_t g_core1_stack[CORE1_STACK_WORDS] __attribute__((aligned(8)));

/* ---- shared with Core 0: the emulated screen ------------------------------- */
static tdsr_termcell_t g_vram[25][80];
static volatile int g_cx, g_cy;

/* Core 1 liveness: bumped in its idle hook (which runs even while an utterance
 * blocks on the full ring).  Core 0 relaunches Core 1 if it stops for 5 s, so a
 * hang inside a speech engine costs the user a restart announcement, not the
 * machine.  The emulated machine keeps running throughout. */
static volatile uint32_t g_core1_heartbeat;
static volatile uint32_t g_core1_restarts;

/* ---- Core 1: audio ISR --------------------------------------------------- */
static uint g_slice;
static bool audio_tick(repeating_timer_t *t)
{
    (void)t;
    int16_t s = audio_output_pop_sample();
    uint16_t level = (uint16_t)(((int32_t)s + 32768) >> 5);   /* 16-bit -> 11-bit unsigned */
    pwm_set_both_levels(g_slice, level, level);
    return true;
}

static void core1_idle(void)
{
    g_core1_heartbeat++;
    tdsr_capture_termcells(&g_vram[0][0], 25, 80, g_cx, g_cy);
    tdsr_service(to_ms_since_boot(get_absolute_time()));
    __wfe();
}

static void core1_main(void)
{
    static repeating_timer_t timer;
    alarm_pool_t *pool;

    gpio_set_function(PWM_L_PIN, GPIO_FUNC_PWM);
    gpio_set_function(PWM_R_PIN, GPIO_FUNC_PWM);
    g_slice = pwm_gpio_to_slice_num(PWM_L_PIN);
    pwm_set_wrap(g_slice, PWM_WRAP);
    pwm_set_both_levels(g_slice, PWM_WRAP / 2, PWM_WRAP / 2);
    pwm_set_enabled(g_slice, true);

    pool = alarm_pool_create_with_unused_hardware_alarm(4);        /* Core 1's own alarms */
    alarm_pool_add_repeating_timer_us(pool, -1000000 / AUDIO_RATE, audio_tick, NULL, &timer);

    espk_data_set_blob(espk_dat_blob, (long)(espk_dat_end - espk_dat_blob));
    core1_set_idle_hook(core1_idle);
    core1_speech_init(SYNTH_ENGINE_DECTALK, AUDIO_RATE);
    tdsr_init();
    if (g_core1_restarts == 0) {
        core1_speech_announce_boot("Talking CP M");
    } else {
        core1_chime();
        core1_say("Speech restarted.", false);
    }

    for (;;) {
        core1_speech_poll(to_ms_since_boot(get_absolute_time()));
        core1_idle();
    }
}

/* Core 0: relaunch Core 1 when its heartbeat stops (call every second or so). */
static void core1_watchdog(void)
{
    static uint32_t last_beat, stuck_ticks;
    uint32_t beat = g_core1_heartbeat;
    if (beat != last_beat) { last_beat = beat; stuck_ticks = 0; return; }
    if (++stuck_ticks < 5) return;                       /* 5 s without a heartbeat */
    stuck_ticks = 0;
    g_core1_restarts++;
    multicore_reset_core1();
    multicore_launch_core1_with_stack(core1_main, g_core1_stack, sizeof g_core1_stack);
}

/* ---- Core 0: stand-in for the emulated machine ------------------------------ */
static void vram_clear(void)
{
    for (int r = 0; r < 25; r++) for (int c = 0; c < 80; c++) { g_vram[r][c].ch = ' '; g_vram[r][c].fg = 7; g_vram[r][c].bg = 0; }
    g_cx = g_cy = 0;
}
static void vram_scroll(void)
{
    memmove(&g_vram[0][0], &g_vram[1][0], sizeof g_vram[0] * 24);
    for (int c = 0; c < 80; c++) { g_vram[24][c].ch = ' '; g_vram[24][c].bg = 0; }
}
static void vram_putc(char ch)
{
    if (ch == '\r') { g_cx = 0; return; }
    if (ch == '\n') { if (++g_cy > 24) { vram_scroll(); g_cy = 24; } return; }
    if (ch == 8) { if (g_cx > 0) g_cx--; g_vram[g_cy][g_cx].ch = ' '; return; }
    g_vram[g_cy][g_cx].ch = ch;
    if (++g_cx >= 80) { g_cx = 0; if (++g_cy > 24) { vram_scroll(); g_cy = 24; } }
}
static void vram_puts(const char *s) { while (*s) vram_putc(*s++); }

int main(void)
{
    stdio_init_all();
    vram_clear();
    vram_puts("192K CP/M VERS. 2.2 (Talking)\r\nA>");
    multicore_launch_core1_with_stack(core1_main, g_core1_stack, sizeof g_core1_stack);

    absolute_time_t next_demo = make_timeout_time_ms(8000);
    absolute_time_t next_watchdog = make_timeout_time_ms(1000);
    int demo = 0;
    for (;;) {
        int ch = getchar_timeout_us(1000);
        if (absolute_time_diff_us(get_absolute_time(), next_watchdog) < 0) {
            next_watchdog = make_timeout_time_ms(1000);
            core1_watchdog();
        }
        if (ch != PICO_ERROR_TIMEOUT) {
            uint8_t mods = 0;
            if (ch == 0x1B) {                                   /* ESC x = Alt+x from a terminal */
                int n = getchar_timeout_us(20000);
                if (n != PICO_ERROR_TIMEOUT) { ch = n; mods = DT_MOD_ALT; }
            }
            if (!doubletalk_key_event((uint8_t)ch, mods) && !(mods & DT_MOD_ALT)) {
                vram_putc((char)ch);                            /* the "emulated OS" echoes it */
                if (ch == '\r') vram_puts("\n\r\nA>");
            }
        }
        if (absolute_time_diff_us(get_absolute_time(), next_demo) < 0) {
            next_demo = make_timeout_time_ms(15000);
            if (demo++ == 0) {
                vram_puts("DIR\r\nA: SPEAK    COM : WS       COM : SC       COM : MBASIC   COM\r\nA>");
            } else {
                /* a CP/M program talking to the synthesizer directly (BDOS 224 / SPEAK.COM) */
                doubletalk_write_string("\x01" "6S" "Hello from the emulated machine on core zero.\r");
            }
        }
        tight_loop_contents();
    }
}
