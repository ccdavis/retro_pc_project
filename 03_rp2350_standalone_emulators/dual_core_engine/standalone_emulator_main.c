/**
 * standalone_emulator_main.c -- dual-core skeleton for the standalone RP2350-Zero.
 *
 *   Core 1: HSTX DVI 640x480 output (common/hstx_dvi.c).  The DMA interrupt asks
 *           core1_line() for each display line; it renders the emulated machine's
 *           scanline with rp2350_hstx_beamracer.h, doubled vertically and centred.
 *   Core 0: owns the emulated memory and will run the CPU core.  THIS FILE HAS NO
 *           CPU EMULATION: the loop only services the Beetle UART (13-byte HID
 *           frames, see 01_ti994a_emulator/rp2350_pico994a/uart_hid_bridge.h) and
 *           draws a test pattern into the emulated RAM so the video path can be
 *           seen.  Drop a 65C02 / 8088 / 6510 / TMS9900 core into core0_run().
 *
 * Compile-checked with the Pico SDK (CMakeLists.txt in this directory); not run on
 * hardware yet.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hstx_dvi.h"
#include "rp2350_hstx_beamracer.h"

#ifndef ENABLE_BEETLE_COPROCESSOR
#define ENABLE_BEETLE_COPROCESSOR 1
#endif
#define BEETLE_UART_ID     uart0
#define BEETLE_UART_TX_PIN 0
#define BEETLE_UART_RX_PIN 1
#define BEETLE_UART_BAUD   115200

typedef enum {
    MACHINE_APPLE_IIE_IIC = 0,
    MACHINE_IBM_PCJR_XT   = 1,
    MACHINE_COMMODORE_64  = 2,
    MACHINE_TI99_4A       = 3
} MachineProfile;

#ifndef SELECTED_MACHINE
#define SELECTED_MACHINE MACHINE_TI99_4A
#endif

/* 256 KB emulated RAM pool in SRAM; the two HSTX line buffers live in hstx_dvi.c */
static uint8_t g_system_ram[256 * 1024] __attribute__((aligned(4)));
static uint8_t g_aux_regs[64];          /* VIC-II $D000.. / TMS9918A R0..R7 / soft switches */
static uint8_t g_vic_bank __attribute__((unused)) = 0;

/* ---- Core 1: one display line ------------------------------------------- */
static void core1_line(uint16_t *line, unsigned y)
{
#if SELECTED_MACHINE == MACHINE_APPLE_IIE_IIC
    /* 192 lines doubled = 384, centred in 480 (48-line borders) */
    if (y < 48 || y >= 432) { fill_line(line, 0, 640, A2_BLACK); return; }
    render_scanline_apple2_hires(line, (uint8_t)((y - 48) >> 1), &g_system_ram[0x00000], &g_system_ram[0x10000], false, false);
#elif SELECTED_MACHINE == MACHINE_IBM_PCJR_XT
    if (y < 40 || y >= 440) { fill_line(line, 0, 640, g_palette_cga[0]); return; }
    render_scanline_pcjr_tga16(line, (uint8_t)((y - 40) >> 1), g_system_ram, 0x18000);
#elif SELECTED_MACHINE == MACHINE_COMMODORE_64
    if (y < 40 || y >= 440) { fill_line(line, 0, 640, g_palette_c64[g_aux_regs[0x20] & 0x0F]); return; }
    {
        uint8_t ry = (uint8_t)((y - 40) >> 1);
        g_beam_sync.badline_stall_cpu = ((ry & 7) == (g_aux_regs[0x11] & 7)) && ((y & 1) == 0);
        render_scanline_c64_vic2(line, ry, &g_system_ram[0x00000], &g_system_ram[0x10000], &g_system_ram[0x11000], g_aux_regs, g_vic_bank);
    }
#else /* TI-99/4A */
    if (y < 48 || y >= 432) { uint8_t bd = g_aux_regs[7] & 0x0F; fill_line(line, 0, 640, g_palette_tms9918[bd ? bd : 1]); return; }
    render_scanline_ti99_tms9918a(line, (uint8_t)((y - 48) >> 1), &g_system_ram[0x00000], g_aux_regs);
#endif
    g_beam_sync.current_raster_line = (uint16_t)y;
    if (y == 479) g_beam_sync.tms_vdp_status |= 0x80;      /* VBlank */
}

static void core1_main(void)
{
    hstx_dvi_init(core1_line);          /* the DMA IRQ now drives everything on this core */
    for (;;) __wfe();
}

/* ---- Core 0: Beetle UART HID frames ----------------------------------------- */
#if ENABLE_BEETLE_COPROCESSOR
static uint8_t s_rx_frame[13], s_rx_idx, s_kbd_report[8], s_joy_mask;

static void beetle_init(void)
{
    uart_init(BEETLE_UART_ID, BEETLE_UART_BAUD);
    gpio_set_function(BEETLE_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(BEETLE_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(BEETLE_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(BEETLE_UART_ID, true);
}

static void beetle_poll(void)
{
    while (uart_is_readable(BEETLE_UART_ID)) {
        uint8_t b = uart_getc(BEETLE_UART_ID);
        if (s_rx_idx == 0) { if (b == 0xAA) s_rx_frame[s_rx_idx++] = b; }
        else if (s_rx_idx == 1) { if (b == 0x55) s_rx_frame[s_rx_idx++] = b; else s_rx_idx = (b == 0xAA) ? 1 : 0; }
        else {
            s_rx_frame[s_rx_idx++] = b;
            if (s_rx_idx == 13) {
                uint8_t csum = 0;
                for (int i = 2; i <= 11; i++) csum ^= s_rx_frame[i];
                if (csum == s_rx_frame[12] && s_rx_frame[2] == 0x01) {
                    memcpy(s_kbd_report, &s_rx_frame[3], 8);
                    s_joy_mask = s_rx_frame[11];
                }
                s_rx_idx = 0;
            }
        }
    }
}
#endif

/* Test content so the video path shows something before a CPU core exists. */
static void load_test_pattern(void)
{
#if SELECTED_MACHINE == MACHINE_TI99_4A
    /* Graphics I: name table at 0, colour table 0x380, patterns 0x800, backdrop dark blue */
    uint8_t *v = g_system_ram;
    g_aux_regs[0] = 0x00; g_aux_regs[1] = 0xE0; g_aux_regs[2] = 0x00; g_aux_regs[3] = 0x0E;
    g_aux_regs[4] = 0x01; g_aux_regs[5] = 0x06; g_aux_regs[6] = 0x00; g_aux_regs[7] = 0x14;
    for (int i = 0; i < 768; i++) v[i] = (uint8_t)(i & 0xFF);
    for (int i = 0; i < 32; i++) v[0x380 + i] = (uint8_t)(((i & 15) << 4) | 0x01);
    for (int c = 0; c < 256; c++) for (int r = 0; r < 8; r++) v[0x800 + c * 8 + r] = (uint8_t)((c * 37 + r * 13) & 0xFF);
    v[0x300] = 0xD0;                                     /* no sprites */
#elif SELECTED_MACHINE == MACHINE_COMMODORE_64
    g_aux_regs[0x18] = 0x14; g_aux_regs[0x20] = 14; g_aux_regs[0x21] = 6; g_aux_regs[0x11] = 0x1B;
    for (int i = 0; i < 1000; i++) { g_system_ram[0x400 + i] = (uint8_t)(i & 0xFF); g_system_ram[0x10000 + i] = (uint8_t)(1 + (i % 15)); }
    for (int i = 0; i < 4096; i++) g_system_ram[0x11000 + i] = (uint8_t)(0xAA ^ (i & 7) * 0x11);
#else
    for (int i = 0; i < 0x2000; i++) g_system_ram[0x2000 + i] = (uint8_t)(i * 7);
    for (int i = 0; i < 0x8000; i++) g_system_ram[0x18000 + i] = (uint8_t)(i & 0xFF);
#endif
}

int main(void)
{
    stdio_init_all();
    memset(g_system_ram, 0, sizeof g_system_ram);
    load_test_pattern();
#if ENABLE_BEETLE_COPROCESSOR
    beetle_init();
#endif
    multicore_launch_core1(core1_main);

    for (;;) {
#if ENABLE_BEETLE_COPROCESSOR
        beetle_poll();
#endif
        if (g_beam_sync.badline_stall_cpu) g_beam_sync.badline_stall_cpu = false;   /* CPU core: skip 40 cycles here */
        /* core0_run(): emulated CPU goes here */
        tight_loop_contents();
    }
}
