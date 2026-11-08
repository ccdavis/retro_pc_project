/* hstx_dvi.c -- see hstx_dvi.h.  Derived from pico-examples dvi_out_hstx_encoder.c,
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd., SPDX-License-Identifier: BSD-3-Clause. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hstx_dvi.h"

/* ---- 640x480p60 timing ------------------------------------------------- */
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 640
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  480
#define MODE_V_TOTAL_LINES  (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH + MODE_V_ACTIVE_LINES)

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH, SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,  SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS), SYNC_V1_H1,
    HSTX_CMD_NOP
};
static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH, SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,  SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS), SYNC_V0_H1,
    HSTX_CMD_NOP
};
static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH, SYNC_V1_H1, HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,  SYNC_V1_H0, HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,  SYNC_V1_H1,
    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS
};

#define DMACH_PING 0
#define DMACH_PONG 1

static uint16_t s_line[2][MODE_H_ACTIVE_PIXELS] __attribute__((aligned(4)));
static volatile hstx_dvi_line_fn s_fn = NULL;
static volatile uint32_t s_frames = 0;
static bool s_dma_pong = false;
static uint s_v_scanline = 2;
static bool s_vactive_cmdlist_posted = false;

static void __scratch_x("hstx_dvi") dma_irq_handler(void)
{
    uint ch_num = s_dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    s_dma_pong = !s_dma_pong;

    if (s_v_scanline >= MODE_V_FRONT_PORCH && s_v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
        if (s_v_scanline == MODE_V_FRONT_PORCH) s_frames++;
    } else if (s_v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!s_vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        s_vactive_cmdlist_posted = true;
    } else {
        unsigned y = s_v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
        uint16_t *buf = s_line[y & 1];
        hstx_dvi_line_fn fn = s_fn;
        if (fn) fn(buf, y); else memset(buf, 0, sizeof s_line[0]);
        ch->read_addr = (uintptr_t)buf;
        ch->transfer_count = MODE_H_ACTIVE_PIXELS * 2 / sizeof(uint32_t);
        s_vactive_cmdlist_posted = false;
    }
    if (!s_vactive_cmdlist_posted) s_v_scanline = (s_v_scanline + 1) % MODE_V_TOTAL_LINES;
}

void hstx_dvi_set_line_callback(hstx_dvi_line_fn fn) { s_fn = fn; }
uint32_t hstx_dvi_frame_count(void) { return s_frames; }
void hstx_dvi_wait_vblank(void) { uint32_t f = s_frames; while (s_frames == f) tight_loop_contents(); }

void hstx_dvi_init(hstx_dvi_line_fn fn)
{
    s_fn = fn;

    /* 252 MHz system clock, clk_hstx = 126 MHz: 2 bits per clock = 252 Mbit/s TMDS */
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(2);
    set_sys_clock_khz(252000, true);
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, 252 * MHZ, 126 * MHZ);

    /* TMDS encoder for RGB565 pixels: after a right rotation by ROT the lane takes
     * NBITS from bit 7 downwards.  R 15:11 -> rot 8, G 10:5 -> rot 3, B 4:0 -> rot 29. */
    hstx_ctrl_hw->expand_tmds =
        5  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB | 8  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        6  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | 3  << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        5  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB | 29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
    /* two 16-bit pixels per 32-bit word; control symbols are whole words */
    hstx_ctrl_hw->expand_shift =
        2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB | 16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB | 0  << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    /* lane -> GPIO pair.  HSTX bit n is GPIO 12 + n. */
#ifdef HSTX_DVI_PINOUT_M2
    /* frank-apple M2: CK 12(N)/13(P), D0 14(N)/15(P), D1 16(N)/17(P), D2 18(N)/19(P) */
    static const int lane_bit[3] = { 2, 4, 6 };
    const int clk_bit = 0;
    const bool n_first = true;
#else
    /* Pico DVI Sock / PicoPlus TinyPCB: D0 12(P)/13(N), CK 14(P)/15(N), D2 16(P)/17(N), D1 18(P)/19(N) */
    static const int lane_bit[3] = { 0, 6, 4 };
    const int clk_bit = 2;
    const bool n_first = false;
#endif
    hstx_ctrl_hw->bit[clk_bit + (n_first ? 1 : 0)] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[clk_bit + (n_first ? 0 : 1)] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
        uint32_t sel = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB | (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[lane_bit[lane] + (n_first ? 1 : 0)] = sel;
        hstx_ctrl_hw->bit[lane_bit[lane] + (n_first ? 0 : 1)] = sel | HSTX_CTRL_BIT0_INV_BITS;
    }
    for (int i = 12; i <= 19; ++i) gpio_set_function(i, 0);   /* GPIO_FUNC_HSTX */

    memset(s_line, 0, sizeof s_line);
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_priority(DMA_IRQ_0, 0);
    irq_set_enabled(DMA_IRQ_0, true);
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    dma_channel_start(DMACH_PING);
}
