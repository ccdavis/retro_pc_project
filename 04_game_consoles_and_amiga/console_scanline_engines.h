/**
 * console_scanline_engines.h -- Core 1 scanline renderers for consoles and the Mac.
 *
 * One emulated scanline into a 640-pixel RGB565 line for common/hstx_dvi.c.
 *   1. Atari 7800 MARIA: 160A direct and indirect (character) display-list objects
 *      into a 160-byte line RAM, as the chip does.  Not implemented: 160B, the 320
 *      modes, holey DMA, Kangaroo mode, the DLL walk (the caller supplies the zone's
 *      display list and the line offset within the zone).
 *   2. Atari 5200 / 800 GTIA: 4 players and 4 missiles with SIZEP/SIZEM widths over
 *      an ANTIC playfield that the caller has already drawn.  No priority register,
 *      no fifth player, no collisions.
 *   3. ColecoVision controller keypad encoding.
 *   4. Macintosh 128K/512K 512x342 1-bit framebuffer centred in 640x480.
 */
#ifndef CONSOLE_SCANLINE_ENGINES_H
#define CONSOLE_SCANLINE_ENGINES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================================
 * 1. Atari 7800 MARIA
 *    Header byte 1: bits 4..0 = width (two's complement, 0 means a 5-byte header),
 *    bit 6 set in 5-byte headers, bit 5 = indirect (character) mode, bit 7 = write mode.
 *    A header whose byte 1 is 0 ends the list.
 * ========================================================================== */
typedef struct {
    uint16_t dl_addr;        /* display list for the current zone */
    uint8_t  zone_offset;    /* line within the zone, added to the high address byte */
    uint8_t  charbase;       /* CHARBASE register (high byte of character graphics) */
    bool     cwidth2;        /* CTRL bit 4: 2-byte characters in indirect mode */
} maria_zone_t;

static inline void render_scanline_atari7800_maria(uint16_t *out, const uint8_t *mem_64k,
                                                   const maria_zone_t *z, const uint16_t *palette32)
{
    uint8_t line_ram[160];
    memset(line_ram, 0, sizeof line_ram);           /* colour 0 = background */

    uint16_t dl = z->dl_addr;
    for (int objs = 0; objs < 64; objs++) {
        uint8_t b0 = mem_64k[dl], b1 = mem_64k[(uint16_t)(dl + 1)];
        if (b1 == 0) break;
        bool ext = (b1 & 0x1F) == 0;                  /* 5-byte header */
        bool indirect = ext && (b1 & 0x20);
        uint8_t pal_width = ext ? mem_64k[(uint16_t)(dl + 3)] : b1;
        uint8_t hi = mem_64k[(uint16_t)(dl + 2)];
        uint8_t hpos = mem_64k[(uint16_t)(dl + (ext ? 4 : 3))];
        uint8_t palette = (pal_width >> 5) & 7;
        int width = 32 - (pal_width & 0x1F);
        if (width == 32 && !ext) width = 32;          /* width field 0 only occurs in 5-byte headers */
        dl += ext ? 5 : 4;

        for (int w = 0; w < width; w++) {
            uint16_t gfx;
            if (indirect) {
                /* b0..: character indices; graphics at (CHARBASE + offset) << 8 | index */
                uint8_t ch = mem_64k[(uint16_t)((hi << 8 | b0) + w)];
                gfx = (uint16_t)(((uint8_t)(z->charbase + z->zone_offset)) << 8 | ch);
            } else {
                gfx = (uint16_t)(((uint8_t)(hi + z->zone_offset)) << 8 | (uint8_t)(b0 + w));
            }
            int bytes = (indirect && z->cwidth2) ? 2 : 1;
            for (int k = 0; k < bytes; k++) {
                uint8_t g = mem_64k[(uint16_t)(gfx + k)];
                for (int px = 0; px < 4; px++) {
                    uint8_t cp = (g >> (6 - px * 2)) & 3;
                    if (!cp) continue;                 /* 0 is transparent */
                    uint8_t x = (uint8_t)(hpos + (w * bytes + k) * 4 + px);
                    if (x < 160) line_ram[x] = (uint8_t)((palette << 2) | cp);
                }
            }
        }
    }
    for (int x = 0; x < 160; x++) {
        uint16_t rgb = palette32[line_ram[x] & 31];
        out[x * 4] = rgb; out[x * 4 + 1] = rgb; out[x * 4 + 2] = rgb; out[x * 4 + 3] = rgb;
    }
}

/* ============================================================================
 * 2. Atari 5200 / 800 GTIA players and missiles over a 160-colour-clock playfield
 *    hpos: colour clocks, 48 = left edge of the normal playfield.  size: 0 normal,
 *    1 double, 3 quadruple (SIZEP0..3, SIZEM 2 bits per missile).
 * ========================================================================== */
typedef struct {
    uint8_t  hposp[4], grafp[4], sizep[4];
    uint8_t  hposm[4], grafm, sizem;
    uint16_t colpm[4];                 /* RGB565 */
} gtia_pmg_t;

static inline int gtia_width(uint8_t size) { return size == 3 ? 4 : (size == 1 ? 2 : 1); }

static inline void gtia_draw(uint16_t *inout, int hpos, uint8_t bits, int nbits, int width, uint16_t rgb)
{
    int x = hpos - 48;
    for (int b = nbits - 1; b >= 0; b--) {
        bool on = (bits >> b) & 1;
        for (int w = 0; w < width; w++, x++) {
            if (!on || x < 0 || x >= 160) continue;
            inout[x * 4] = rgb; inout[x * 4 + 1] = rgb; inout[x * 4 + 2] = rgb; inout[x * 4 + 3] = rgb;
        }
    }
}

static inline void overlay_scanline_atari5200_gtia_pmg(uint16_t *inout, const gtia_pmg_t *g)
{
    /* missiles share their player's colour; lowest-numbered player has priority, so draw 3 first */
    for (int p = 3; p >= 0; p--) {
        uint8_t m = (g->grafm >> (p * 2)) & 3;
        if (m) gtia_draw(inout, g->hposm[p], m, 2, gtia_width((g->sizem >> (p * 2)) & 3), g->colpm[p]);
    }
    for (int p = 3; p >= 0; p--) {
        if (g->grafp[p]) gtia_draw(inout, g->hposp[p], g->grafp[p], 8, gtia_width(g->sizep[p] & 3), g->colpm[p]);
    }
}

/* ============================================================================
 * 3. ColecoVision controller, keypad mode (Z80 IN A,(FCh)/(FFh)): bits 3..0 keypad
 *    code, bit 6 right fire (active low), other bits read as 1.
 * ========================================================================== */
static inline uint8_t colecovision_encode_keypad(uint8_t usb_hid_keycode, bool fire_right)
{
    uint8_t code;
    switch (usb_hid_keycode) {
        case 0x1E: code = 0x0D; break;   /* 1 */
        case 0x1F: code = 0x07; break;   /* 2 */
        case 0x20: code = 0x0C; break;   /* 3 */
        case 0x21: code = 0x02; break;   /* 4 */
        case 0x22: code = 0x03; break;   /* 5 */
        case 0x23: code = 0x0E; break;   /* 6 */
        case 0x24: code = 0x05; break;   /* 7 */
        case 0x25: code = 0x01; break;   /* 8 */
        case 0x26: code = 0x0B; break;   /* 9 */
        case 0x27: code = 0x0A; break;   /* 0 */
        case 0x2D: code = 0x09; break;   /* - -> * */
        case 0x2E: code = 0x06; break;   /* = -> # */
        default:   code = 0x0F; break;   /* none */
    }
    return (uint8_t)((fire_right ? 0x30 : 0x70) | code);
}

/* ============================================================================
 * 4. Macintosh 512x342, 1 bit per pixel, MSB first, 1 = black; 64 bytes per row.
 * ========================================================================== */
static inline void render_scanline_macintosh_512x342(uint16_t *out, uint16_t hdmi_y, const uint8_t *mac_vram)
{
    if (hdmi_y < 69 || hdmi_y >= 69 + 342) { memset(out, 0, 640 * sizeof(uint16_t)); return; }
    memset(out, 0, 64 * sizeof(uint16_t));
    memset(out + 576, 0, 64 * sizeof(uint16_t));
    const uint8_t *row = mac_vram + (hdmi_y - 69) * 64;
    uint16_t *dst = out + 64;
    for (int b = 0; b < 64; b++)
        for (int bit = 7; bit >= 0; bit--) *dst++ = (row[b] >> bit) & 1 ? 0x0000 : 0xFFFF;
}

#endif /* CONSOLE_SCANLINE_ENGINES_H */
