/*
 * hstx_dvi.h -- RP2350 HSTX DVI output, 640x480 at 60 Hz, 16-bit RGB565 scanlines
 * rendered on demand.  Shared by the HDMI terminal card (project 2), the
 * standalone emulator (project 3) and, later, the talking machine (project 5).
 *
 * Based on pico-examples/hstx/dvi_out_hstx_encoder.c (Raspberry Pi, BSD-3-Clause):
 * two DMA channels ping-pong HSTX command lists and pixel lines into the HSTX
 * FIFO; the DMA IRQ asks the application for the next active line through a
 * callback, so no framebuffer is needed (two 1280-byte line buffers).
 *
 * Pinout (the PicoPlus / Pico DVI Sock order, which pico-994A HW_CONFIG 6 and
 * pico-infonesPlus use on the RP2350-Zero):
 *   GP12 D0+  GP13 D0-   GP14 CK+  GP15 CK-   GP16 D2+  GP17 D2-   GP18 D1+  GP19 D1-
 * frank-apple's M2 layout differs (CK on 12/13, D0 14/15, D1 16/17, D2 18/19, N
 * before P); define HSTX_DVI_PINOUT_M2 to use that order instead.
 *
 * Clocking: the system clock is raised to 252 MHz (as the PicoPlus and
 * Murmulator firmware do) and clk_hstx runs at clk_sys / 2 = 126 MHz; with two
 * bits shifted per HSTX clock that is the exact 252 Mbit/s TMDS rate of 480p60.
 *
 * Compile-checked with the Pico SDK; not yet run on hardware (QEMU has no HSTX).
 */
#ifndef HSTX_DVI_H
#define HSTX_DVI_H
#include <stdint.h>
#include <stdbool.h>

#define HSTX_DVI_WIDTH  640
#define HSTX_DVI_HEIGHT 480

/* Fill 'line' (640 RGB565 pixels) for display row 'y' (0..479).  Runs in the DMA
 * IRQ on the core that called hstx_dvi_init(); keep it short (one line of text
 * or one emulated scanline).  A NULL callback shows black. */
typedef void (*hstx_dvi_line_fn)(uint16_t *line, unsigned y);

void hstx_dvi_init(hstx_dvi_line_fn fn);
void hstx_dvi_set_line_callback(hstx_dvi_line_fn fn);
uint32_t hstx_dvi_frame_count(void);           /* increments once per vertical blank */
void hstx_dvi_wait_vblank(void);

static inline uint16_t hstx_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
#endif
