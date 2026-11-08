/**
 * video_composite_spi.h -- Video Option 1: 2-resistor monochrome NTSC composite.
 *
 * STATUS: NOT WORKING, kept as a documented stub.  The first draft generated the
 * scanlines inside a 64 us timer interrupt with Arduino's SPI.writeBytes(), which
 * takes a FreeRTOS mutex (not allowed in an ISR) and blocks for ~53 us of every
 * 64 us line (83 % of one core), and it drew glyphs from an arithmetic hash rather
 * than a font.  On a single-core ESP32-C3/C6 that also runs the BLE host and Wi-Fi
 * this approach cannot work: the radio stacks starve and the watchdog resets the
 * chip.  A working design needs a hardware stream (GDMA-fed SPI or the LCD/I2S
 * peripheral on chips that have it) producing the whole frame, including sync,
 * without CPU per line, and a two-pin resistor DAC for the three levels.  That is
 * a project of its own; for the AirTag build use Video Option 3 (the RP2350
 * HDMI card), which costs the Beetle nothing.
 *
 * What is kept here and does work: the 80x25 VT100/ANSI text model (video_putc /
 * video_write / video_write_buf), which the two sketches use, and a proper 8x8
 * font (public domain font8x8_basic) so that whoever wires up a DMA engine can
 * render from `composite_render_scanline()`.
 *
 * Define COMPOSITE_EXPERIMENTAL to compile it without the #error.
 */
#ifndef VIDEO_COMPOSITE_SPI_H
#define VIDEO_COMPOSITE_SPI_H

#include <Arduino.h>
#include "../../common/font8x8_basic.h"

#ifndef COMPOSITE_EXPERIMENTAL
#error "The composite backend is a non-working stub (see the comment at the top of video_composite_spi.h). Use VIDEO_BACKEND_UART_RP2350_HDMI, or define COMPOSITE_EXPERIMENTAL to build the text model only."
#endif

#define TERM_COLS 80
#define TERM_ROWS 25

static char    s_vram[TERM_ROWS][TERM_COLS];
static uint8_t s_cursor_x, s_cursor_y;
enum VtParseState { VT_STATE_GROUND = 0, VT_STATE_ESC, VT_STATE_CSI };
static VtParseState s_vt_state = VT_STATE_GROUND;
static int s_csi_params[4], s_csi_param_count;

static void term_scroll_up() { memmove(&s_vram[0][0], &s_vram[1][0], (TERM_ROWS - 1) * TERM_COLS); memset(&s_vram[TERM_ROWS - 1][0], ' ', TERM_COLS); }
static void term_clear() { memset(s_vram, ' ', sizeof s_vram); s_cursor_x = s_cursor_y = 0; }

static void term_handle_csi(char fin)
{
    int p0 = (s_csi_param_count >= 1 && s_csi_params[0] > 0) ? s_csi_params[0] : 1;
    int p1 = (s_csi_param_count >= 2 && s_csi_params[1] > 0) ? s_csi_params[1] : 1;
    switch (fin) {
        case 'A': s_cursor_y = s_cursor_y >= p0 ? s_cursor_y - p0 : 0; break;
        case 'B': s_cursor_y = s_cursor_y + p0 < TERM_ROWS ? s_cursor_y + p0 : TERM_ROWS - 1; break;
        case 'C': s_cursor_x = s_cursor_x + p0 < TERM_COLS ? s_cursor_x + p0 : TERM_COLS - 1; break;
        case 'D': s_cursor_x = s_cursor_x >= p0 ? s_cursor_x - p0 : 0; break;
        case 'H': case 'f': s_cursor_y = constrain(p0 - 1, 0, TERM_ROWS - 1); s_cursor_x = constrain(p1 - 1, 0, TERM_COLS - 1); break;
        case 'J':
            if (s_csi_params[0] == 2) term_clear();
            else { for (int c = s_cursor_x; c < TERM_COLS; c++) s_vram[s_cursor_y][c] = ' '; for (int r = s_cursor_y + 1; r < TERM_ROWS; r++) memset(s_vram[r], ' ', TERM_COLS); }
            break;
        case 'K': for (int c = s_cursor_x; c < TERM_COLS; c++) s_vram[s_cursor_y][c] = ' '; break;
        default: break;   /* 'm' colours: monochrome */
    }
}

static inline void video_putc(char c)
{
    Serial.write(c);
    if (s_vt_state == VT_STATE_GROUND) {
        if (c == '\x1B') s_vt_state = VT_STATE_ESC;
        else if (c == '\r') s_cursor_x = 0;
        else if (c == '\n') { if (++s_cursor_y >= TERM_ROWS) { term_scroll_up(); s_cursor_y = TERM_ROWS - 1; } }
        else if (c == '\b' || c == 0x7F) { if (s_cursor_x > 0) s_vram[s_cursor_y][--s_cursor_x] = ' '; }
        else if (c >= 0x20 && c <= 0x7E) {
            s_vram[s_cursor_y][s_cursor_x] = c;
            if (++s_cursor_x >= TERM_COLS) { s_cursor_x = 0; if (++s_cursor_y >= TERM_ROWS) { term_scroll_up(); s_cursor_y = TERM_ROWS - 1; } }
        }
    } else if (s_vt_state == VT_STATE_ESC) {
        if (c == '[') { s_vt_state = VT_STATE_CSI; memset(s_csi_params, 0, sizeof s_csi_params); s_csi_param_count = 1; }
        else { if (c == 'c') term_clear(); s_vt_state = VT_STATE_GROUND; }
    } else {
        if (c >= '0' && c <= '9') { int i = s_csi_param_count - 1; if (i < 4) s_csi_params[i] = s_csi_params[i] * 10 + (c - '0'); }
        else if (c == ';') { if (s_csi_param_count < 4) s_csi_param_count++; }
        else if (c >= 0x40 && c <= 0x7E) { term_handle_csi(c); s_vt_state = VT_STATE_GROUND; }
    }
}
static inline void video_write(const char *s) { while (*s) video_putc(*s++); }
static inline void video_write_buf(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) video_putc((char)b[i]); }

/* One 80-byte scanline of 1-bit pixels (bit 7 first on the wire) for text line
 * `line` (0..199).  A DMA frame generator would call this 200 times per frame
 * from a task, never from an interrupt. */
static inline void composite_render_scanline(uint16_t line, uint8_t out[TERM_COLS])
{
    uint8_t row = line >> 3, font_row = line & 7;
    for (int col = 0; col < TERM_COLS; col++) {
        uint8_t ch = (uint8_t)s_vram[row][col];
        uint8_t bits = font8x8_basic[ch & 0x7F][font_row];
        if (col == s_cursor_x && row == s_cursor_y && font_row >= 6) bits = 0xFF;
        uint8_t msb_first = 0;                                  /* font is LSB = left */
        for (int b = 0; b < 8; b++) if (bits & (1 << b)) msb_first |= (uint8_t)(0x80 >> b);
        out[col] = msb_first;
    }
}

static inline void video_init()
{
    term_clear();
    Serial.println("video_composite_spi.h: experimental stub, no video is generated; output mirrored to Serial");
}
#endif
