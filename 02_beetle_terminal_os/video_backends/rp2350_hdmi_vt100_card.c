/*
 * rp2350_hdmi_vt100_card.c -- Video Option 3: the Waveshare RP2350-Zero as an
 * 80x30 colour ANSI/VT100 terminal on Micro-HDMI (DVI), fed over UART.
 *
 *   UART0 RX GPIO 1 (TX GPIO 0 unused but driven), 921600 8N1, from the Beetle.
 *   HSTX GPIO 12..19 -> 640x480 @ 60 Hz DVI (common/hstx_dvi.c, Pico DVI Sock order).
 *
 * Rendering: 80 columns x 30 rows of 8x16 cells (the public-domain font8x8
 * doubled vertically), 16 ANSI colours, blinking underline cursor, drawn one
 * scanline at a time in the HSTX DMA interrupt: no framebuffer, 7.2 KB for the
 * cell grid.  The UART is drained from its own interrupt into a 4 KB ring so
 * bursts at 921600 baud (92 KB/s) are never lost while a line is being drawn.
 *
 * Escape sequences: CSI A B C D H f J K m, ESC c (reset), plus DEC private
 * ?25h/?25l (cursor show/hide).  SGR: 0, 1 (bright), 7 (inverse), 30-37, 40-47,
 * 90-97, 100-107, 39, 49.
 *
 * The same TermCell grid is what the project-5 screen reader snapshots
 * (tdsr_capture_termcells), so this card doubles as the Talking CP/M display.
 *
 * Build: see CMakeLists.txt next to this file.  Compile-checked with the Pico SDK
 * for waveshare_rp2350_zero; not yet run on hardware.
 */
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hstx_dvi.h"
#include "font8x8_basic.h"

#define VT_UART_ID    uart0
#define VT_UART_RX    1
#define VT_UART_TX    0
#define VT_UART_BAUD  921600
#define VT_UART_IRQ   UART0_IRQ

#define COLS 80
#define ROWS 30
#define CELL_H 16

typedef struct {
    char    ch;
    uint8_t fg;   /* ANSI colour 0..15 */
    uint8_t bg;
} TermCell;

static TermCell s_screen[ROWS][COLS];
static int      s_cx, s_cy;
static uint8_t  s_fg = 10, s_bg = 0;     /* bright green on black */
static bool     s_bold, s_inverse, s_cursor_on = true;

/* ANSI 16-colour palette in RGB565 (VGA-like) */
static const uint16_t k_palette[16] = {
    0x0000, 0xA800, 0x0540, 0xA540, 0x0015, 0xA815, 0x0555, 0xAD55,
    0x52AA, 0xFAAA, 0x57EA, 0xFFEA, 0x52BF, 0xFABF, 0x57FF, 0xFFFF
};

/* ---- UART RX ring (IRQ producer, main loop consumer) --------------------- */
#define RX_RING 4096
static uint8_t  s_rx[RX_RING];
static volatile uint32_t s_rx_w, s_rx_r;
static volatile uint32_t s_rx_overruns;

static void on_uart_rx(void)
{
    while (uart_is_readable(VT_UART_ID)) {
        uint8_t c = uart_getc(VT_UART_ID);
        uint32_t next = (s_rx_w + 1) & (RX_RING - 1);
        if (next == s_rx_r) { s_rx_overruns++; continue; }
        s_rx[s_rx_w] = c;
        __dmb();
        s_rx_w = next;
    }
}

/* ---- screen model ---------------------------------------------------------- */
static void cell_clear(TermCell *c) { c->ch = ' '; c->fg = s_fg; c->bg = s_bg; }

static void screen_clear(void)
{
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) cell_clear(&s_screen[r][c]);
    s_cx = s_cy = 0;
}

static void screen_scroll(void)
{
    memmove(&s_screen[0][0], &s_screen[1][0], (ROWS - 1) * COLS * sizeof(TermCell));
    for (int c = 0; c < COLS; c++) cell_clear(&s_screen[ROWS - 1][c]);
}

static void put_char(char ch)
{
    TermCell *c = &s_screen[s_cy][s_cx];
    c->ch = ch;
    c->fg = s_inverse ? s_bg : (uint8_t)(s_fg | (s_bold ? 8 : 0));
    c->bg = s_inverse ? s_fg : s_bg;
    if (++s_cx >= COLS) {
        s_cx = 0;
        if (++s_cy >= ROWS) { screen_scroll(); s_cy = ROWS - 1; }
    }
}

static void sgr(int m)
{
    if (m == 0)       { s_fg = 10; s_bg = 0; s_bold = false; s_inverse = false; }
    else if (m == 1)  s_bold = true;
    else if (m == 22) s_bold = false;
    else if (m == 7)  s_inverse = true;
    else if (m == 27) s_inverse = false;
    else if (m >= 30 && m <= 37)   s_fg = (uint8_t)(m - 30);
    else if (m == 39)              s_fg = 10;
    else if (m >= 40 && m <= 47)   s_bg = (uint8_t)(m - 40);
    else if (m == 49)              s_bg = 0;
    else if (m >= 90 && m <= 97)   s_fg = (uint8_t)(m - 90 + 8);
    else if (m >= 100 && m <= 107) s_bg = (uint8_t)(m - 100 + 8);
}

static void vt100_feed(char c)
{
    static int state = 0;            /* 0 ground, 1 ESC, 2 CSI */
    static int params[8], pcount;
    static bool priv;

    if (state == 0) {
        if (c == 0x1B) state = 1;
        else if (c == '\r') s_cx = 0;
        else if (c == '\n') { if (++s_cy >= ROWS) { screen_scroll(); s_cy = ROWS - 1; } }
        else if (c == '\b') { if (s_cx > 0) s_cx--; }
        else if (c == 0x7F) { if (s_cx > 0) { s_cx--; cell_clear(&s_screen[s_cy][s_cx]); } }
        else if (c == '\t') { s_cx = (s_cx + 8) & ~7; if (s_cx >= COLS) s_cx = COLS - 1; }
        else if (c == 0x0C) screen_clear();
        else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) put_char(c);
        return;
    }
    if (state == 1) {
        if (c == '[') { state = 2; memset(params, 0, sizeof params); pcount = 1; priv = false; }
        else if (c == 'c') { sgr(0); screen_clear(); state = 0; }
        else state = 0;
        return;
    }
    /* CSI */
    if (c == '?') { priv = true; return; }
    if (c >= '0' && c <= '9') { if (pcount <= 8) params[pcount - 1] = params[pcount - 1] * 10 + (c - '0'); return; }
    if (c == ';') { if (pcount < 8) pcount++; return; }
    if (c < 0x40 || c > 0x7E) { state = 0; return; }
    {
        int p0 = params[0] > 0 ? params[0] : 1, p1 = params[1] > 0 ? params[1] : 1;
        switch (c) {
            case 'A': s_cy = s_cy >= p0 ? s_cy - p0 : 0; break;
            case 'B': s_cy = s_cy + p0 < ROWS ? s_cy + p0 : ROWS - 1; break;
            case 'C': s_cx = s_cx + p0 < COLS ? s_cx + p0 : COLS - 1; break;
            case 'D': s_cx = s_cx >= p0 ? s_cx - p0 : 0; break;
            case 'H': case 'f':
                s_cy = p0 - 1 < ROWS ? p0 - 1 : ROWS - 1;
                s_cx = p1 - 1 < COLS ? p1 - 1 : COLS - 1;
                break;
            case 'J':
                if (params[0] == 2) { screen_clear(); }
                else if (params[0] == 1) { for (int r = 0; r < s_cy; r++) for (int x = 0; x < COLS; x++) cell_clear(&s_screen[r][x]); for (int x = 0; x <= s_cx; x++) cell_clear(&s_screen[s_cy][x]); }
                else { for (int x = s_cx; x < COLS; x++) cell_clear(&s_screen[s_cy][x]); for (int r = s_cy + 1; r < ROWS; r++) for (int x = 0; x < COLS; x++) cell_clear(&s_screen[r][x]); }
                break;
            case 'K':
                if (params[0] == 2) for (int x = 0; x < COLS; x++) cell_clear(&s_screen[s_cy][x]);
                else if (params[0] == 1) for (int x = 0; x <= s_cx; x++) cell_clear(&s_screen[s_cy][x]);
                else for (int x = s_cx; x < COLS; x++) cell_clear(&s_screen[s_cy][x]);
                break;
            case 'm': for (int i = 0; i < pcount; i++) sgr(params[i]); break;
            case 'h': if (priv && params[0] == 25) s_cursor_on = true; break;
            case 'l': if (priv && params[0] == 25) s_cursor_on = false; break;
            default: break;
        }
    }
    state = 0;
}

/* ---- scanline renderer (runs in the HSTX DMA IRQ) ------------------------------ */
static void render_line(uint16_t *line, unsigned y)
{
    unsigned row = y / CELL_H, glyph_row = (y % CELL_H) >> 1;
    bool cursor_row = (row == (unsigned)s_cy) && s_cursor_on && ((hstx_dvi_frame_count() >> 5) & 1) && (y % CELL_H) >= 14;
    const TermCell *cells = s_screen[row];
    uint16_t *dst = line;
    for (int col = 0; col < COLS; col++) {
        const TermCell *c = &cells[col];
        uint8_t bits = font8x8_basic[(uint8_t)c->ch & 0x7F][glyph_row];
        uint16_t fg = k_palette[c->fg & 15], bg = k_palette[c->bg & 15];
        if (cursor_row && col == s_cx) bits = 0xFF;
        for (int b = 0; b < 8; b++) *dst++ = (bits >> b) & 1 ? fg : bg;   /* LSB = leftmost pixel */
    }
}

int main(void)
{
    screen_clear();
    hstx_dvi_init(render_line);

    uart_init(VT_UART_ID, VT_UART_BAUD);
    gpio_set_function(VT_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(VT_UART_RX, GPIO_FUNC_UART);
    uart_set_format(VT_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(VT_UART_ID, true);
    irq_set_exclusive_handler(VT_UART_IRQ, on_uart_rx);
    irq_set_enabled(VT_UART_IRQ, true);
    uart_set_irq_enables(VT_UART_ID, true, false);

    {   /* banner until the Beetle talks */
        const char *b = "\x1b[2J\x1b[H\x1b[1;32mRP2350-Zero HDMI VT100 card ready, 921600 8N1 on GPIO 1\x1b[0m\r\n";
        while (*b) vt100_feed(*b++);
    }
    for (;;) {
        while (s_rx_r != s_rx_w) {
            char c = (char)s_rx[s_rx_r];
            s_rx_r = (s_rx_r + 1) & (RX_RING - 1);
            vt100_feed(c);
        }
        __wfe();
    }
}
