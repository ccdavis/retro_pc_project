/**
 * rp2350_hstx_beamracer.h -- Core 1 scanline renderers for the standalone RP2350-Zero.
 *
 * Each function renders ONE emulated scanline into a 640-pixel RGB565 line that
 * common/hstx_dvi.c hands to the HSTX DVI output from its DMA interrupt, so no
 * framebuffer is needed (the driver keeps two 1280-byte line buffers).
 *
 * What is implemented (and what is not; the first draft overstated this):
 *   1. Apple //e, //c: Hi-Res 280x192 with the four NTSC artifact colours chosen by
 *      pixel parity and the high bit (adjacent lit pixels are not merged to white),
 *      Double Hi-Res 560x192 in monochrome.  No text or Lo-Res modes.
 *   2. IBM PCjr / Tandy: 320x200x16 (bank-interleaved by 0x2000).  No CGA 4-colour
 *      or text modes.
 *   3. Commodore 64: standard text mode with screen and character bases from $D018
 *      and the VIC bank, 8 hardware sprites (single colour, priority, both
 *      collision registers).  No multicolour, no X/Y expansion, no bitmap modes,
 *      no scrolling, no border side effects.
 *   4. TI-99/4A TMS9918A: Graphics I and Graphics II tile backdrop, border colour,
 *      32 sprites (8x8 and 16x16, magnification), the 4-per-line limit with the
 *      fifth-sprite status, coincidence, early clock.  No Text or Multicolor mode.
 * Palettes are per system (the first draft used the C64 palette for everything).
 */
#ifndef RP2350_HSTX_BEAMRACER_H
#define RP2350_HSTX_BEAMRACER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define HDMI_WIDTH   640
#define HDMI_HEIGHT  480

/* Shared state between Core 0 (CPU) and Core 1 (video). */
typedef struct {
    volatile uint16_t current_raster_line;
    volatile bool     badline_stall_cpu;       /* VIC-II badline: CPU loses 40 cycles */
    volatile uint8_t  vic_sprite_sprite_coll;  /* $D01E, cleared by Core 0 on read */
    volatile uint8_t  vic_sprite_bg_coll;      /* $D01F */
    volatile uint8_t  tms_vdp_status;          /* 0x80 VBlank, 0x40 fifth sprite (+index), 0x20 coincidence */
} BeamRacerSyncState;

static BeamRacerSyncState g_beam_sync = {0};

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* Commodore 64 (Pepto palette) */
static const uint16_t g_palette_c64[16] = {
    RGB565(0x00,0x00,0x00), RGB565(0xFF,0xFF,0xFF), RGB565(0x68,0x37,0x2B), RGB565(0x70,0xA4,0xB2),
    RGB565(0x6F,0x3D,0x86), RGB565(0x58,0x8D,0x43), RGB565(0x35,0x28,0x79), RGB565(0xB8,0xC7,0x6F),
    RGB565(0x6F,0x4F,0x25), RGB565(0x43,0x39,0x00), RGB565(0x9A,0x67,0x59), RGB565(0x44,0x44,0x44),
    RGB565(0x6C,0x6C,0x6C), RGB565(0x9A,0xD2,0x84), RGB565(0x6C,0x5E,0xB5), RGB565(0x95,0x95,0x95)
};
/* IBM CGA / PCjr / Tandy 16 colours */
static const uint16_t g_palette_cga[16] = {
    RGB565(0x00,0x00,0x00), RGB565(0x00,0x00,0xAA), RGB565(0x00,0xAA,0x00), RGB565(0x00,0xAA,0xAA),
    RGB565(0xAA,0x00,0x00), RGB565(0xAA,0x00,0xAA), RGB565(0xAA,0x55,0x00), RGB565(0xAA,0xAA,0xAA),
    RGB565(0x55,0x55,0x55), RGB565(0x55,0x55,0xFF), RGB565(0x55,0xFF,0x55), RGB565(0x55,0xFF,0xFF),
    RGB565(0xFF,0x55,0x55), RGB565(0xFF,0x55,0xFF), RGB565(0xFF,0xFF,0x55), RGB565(0xFF,0xFF,0xFF)
};
/* TMS9918A (index 0 is transparent: the renderer substitutes the backdrop) */
static const uint16_t g_palette_tms9918[16] = {
    RGB565(0x00,0x00,0x00), RGB565(0x00,0x00,0x00), RGB565(0x21,0xC8,0x42), RGB565(0x5E,0xDC,0x78),
    RGB565(0x54,0x55,0xED), RGB565(0x7D,0x76,0xFC), RGB565(0xD4,0x52,0x4D), RGB565(0x42,0xEB,0xF5),
    RGB565(0xFC,0x55,0x54), RGB565(0xFF,0x79,0x78), RGB565(0xD4,0xC1,0x54), RGB565(0xE6,0xCE,0x80),
    RGB565(0x21,0xB0,0x3B), RGB565(0xC9,0x5B,0xBA), RGB565(0xCC,0xCC,0xCC), RGB565(0xFF,0xFF,0xFF)
};
/* Apple II Hi-Res artifact colours */
#define A2_BLACK  RGB565(0x00,0x00,0x00)
#define A2_WHITE  RGB565(0xFF,0xFF,0xFF)
#define A2_VIOLET RGB565(0xDD,0x22,0xDD)
#define A2_GREEN  RGB565(0x11,0xDD,0x00)
#define A2_BLUE   RGB565(0x22,0x22,0xFF)
#define A2_ORANGE RGB565(0xFF,0x66,0x00)

static inline void fill_line(uint16_t *out, int from, int to, uint16_t c)
{
    for (int x = from; x < to; x++) out[x] = c;
}

/* ============================================================================
 * 1. Apple //e, //c Hi-Res / Double Hi-Res
 *    Woz's interleaved line address: ((y&7)<<10) | (((y>>3)&7)<<7) | ((y>>6)*40)
 * ========================================================================== */
static inline uint16_t apple2_hires_line_offset(uint8_t y)
{
    return (uint16_t)(((y & 0x07) << 10) | (((y >> 3) & 0x07) << 7) | ((y >> 6) * 40));
}

static inline void render_scanline_apple2_hires(uint16_t *out, uint8_t y,
                                                const uint8_t *main_ram_64k, const uint8_t *aux_ram_64k,
                                                bool page2, bool double_hires)
{
    uint16_t base = (page2 ? 0x4000 : 0x2000) + apple2_hires_line_offset(y);
    fill_line(out, 0, 40, A2_BLACK);
    fill_line(out, 600, 640, A2_BLACK);
    uint16_t *dst = out + 40;                                 /* 560 pixels centred */

    if (!double_hires) {
        /* 280 pixels, each doubled.  Colour from pixel parity and the byte's high bit;
         * two adjacent lit pixels render white, as on a composite monitor. */
        uint8_t prev = 0;
        for (int col = 0; col < 40; col++) {
            uint8_t b = main_ram_64k[base + col];
            bool shifted = (b & 0x80) != 0;
            for (int bit = 0; bit < 7; bit++) {
                int x = col * 7 + bit;
                uint8_t px = (b >> bit) & 1;
                uint8_t next = (bit < 6) ? ((b >> (bit + 1)) & 1)
                                         : (col < 39 ? (main_ram_64k[base + col + 1] & 1) : 0);
                uint16_t color = A2_BLACK;
                if (px) {
                    if (prev || next) color = A2_WHITE;
                    else if (x & 1) color = shifted ? A2_ORANGE : A2_GREEN;
                    else            color = shifted ? A2_BLUE   : A2_VIOLET;
                }
                *dst++ = color; *dst++ = color;
                prev = px;
            }
        }
    } else {
        /* 560 x 192: 7 bits from AUX then 7 bits from MAIN per column pair, monochrome */
        for (int col = 0; col < 40; col++) {
            uint16_t pair14 = (uint16_t)((aux_ram_64k[base + col] & 0x7F) | ((main_ram_64k[base + col] & 0x7F) << 7));
            for (int i = 0; i < 14; i++) *dst++ = (pair14 >> i) & 1 ? A2_WHITE : A2_BLACK;
        }
    }
}

/* ============================================================================
 * 2. IBM PCjr / Tandy 1000 320x200x16 (4 banks interleaved by 0x2000, 160 bytes per line)
 * ========================================================================== */
static inline void render_scanline_pcjr_tga16(uint16_t *out, uint8_t y, const uint8_t *sys_ram, uint32_t vram_base)
{
    const uint8_t *src = sys_ram + vram_base + (uint32_t)((y & 3) * 0x2000) + (uint32_t)((y >> 2) * 160);
    for (int x = 0; x < 160; x++) {
        uint8_t b = src[x];
        uint16_t l = g_palette_cga[b >> 4], r = g_palette_cga[b & 0x0F];
        out[x * 4 + 0] = l; out[x * 4 + 1] = l; out[x * 4 + 2] = r; out[x * 4 + 3] = r;
    }
}

/* ============================================================================
 * 3. Commodore 64 VIC-II: standard text mode + 8 sprites with collisions
 *    y: 0..199 (display line; raster = y + 51 on PAL), vic_bank: 0..3 from CIA2 $DD00
 * ========================================================================== */
static inline void render_scanline_c64_vic2(uint16_t *out, uint8_t y,
                                            const uint8_t *c64_ram_64k, const uint8_t *color_ram_1k,
                                            const uint8_t *char_rom_4k, const uint8_t *vic_regs, uint8_t vic_bank)
{
    uint8_t bg_col = vic_regs[0x21] & 0x0F;
    uint16_t bank_base   = (uint16_t)((3 - (vic_bank & 3)) << 14);  /* $DD00 bits are inverted */
    uint16_t screen_base = bank_base + (uint16_t)(((vic_regs[0x18] >> 4) & 0x0F) << 10);
    uint16_t char_base   = (uint16_t)(((vic_regs[0x18] >> 1) & 0x07) << 11);
    /* the character ROM appears to the VIC at $1000-$1FFF of banks 0 and 2 */
    bool rom_chars = ((vic_bank & 1) == 0) && (char_base == 0x1000 || char_base == 0x1800);
    const uint8_t *chars = rom_chars ? char_rom_4k + (char_base - 0x1000) : c64_ram_64k + bank_base + char_base;

    uint8_t bg_mask[320];
    uint8_t spr_owner[320];
    memset(bg_mask, 0, sizeof bg_mask);
    memset(spr_owner, 0xFF, sizeof spr_owner);

    uint8_t text_row = y >> 3, glyph_y = y & 7;
    for (int col = 0; col < 40; col++) {
        uint8_t ch   = c64_ram_64k[screen_base + text_row * 40 + col];
        uint8_t fg   = color_ram_1k[text_row * 40 + col] & 0x0F;
        uint8_t bits = chars[(ch << 3) + glyph_y];
        for (int b = 0; b < 8; b++) {
            int px = col * 8 + b;
            bool set = (bits >> (7 - b)) & 1;
            bg_mask[px] = set;
            uint16_t rgb = g_palette_c64[set ? fg : bg_col];
            out[px * 2] = rgb; out[px * 2 + 1] = rgb;
        }
    }

    uint8_t spr_enable = vic_regs[0x15], spr_msb_x = vic_regs[0x10], spr_prio = vic_regs[0x1B];
    for (int s = 7; s >= 0; s--) {
        if (!(spr_enable & (1 << s))) continue;
        int rel_y = (int)(y + 50) - (int)vic_regs[s * 2 + 1];
        if (rel_y < 0 || rel_y >= 21) continue;
        int base_x = (int)(vic_regs[s * 2] | ((spr_msb_x >> s) & 1 ? 0x100 : 0)) - 24;
        uint8_t spr_ptr = c64_ram_64k[screen_base + 0x03F8 + s];
        const uint8_t *row = &c64_ram_64k[bank_base + spr_ptr * 64 + rel_y * 3];
        uint32_t row24 = ((uint32_t)row[0] << 16) | ((uint32_t)row[1] << 8) | row[2];
        uint16_t rgb = g_palette_c64[vic_regs[0x27 + s] & 0x0F];
        for (int bit = 0; bit < 24; bit++) {
            if (!((row24 >> (23 - bit)) & 1)) continue;
            int px = base_x + bit;
            if (px < 0 || px >= 320) continue;
            if (bg_mask[px]) g_beam_sync.vic_sprite_bg_coll |= (uint8_t)(1 << s);
            if (spr_owner[px] != 0xFF) g_beam_sync.vic_sprite_sprite_coll |= (uint8_t)((1 << s) | (1 << spr_owner[px]));
            spr_owner[px] = (uint8_t)s;
            if (!((spr_prio >> s) & 1) || !bg_mask[px]) { out[px * 2] = rgb; out[px * 2 + 1] = rgb; }
        }
    }
}

/* ============================================================================
 * 4. TI-99/4A TMS9918A: Graphics I / II backdrop + sprites.  y: 0..191.
 *    vdp_regs: the eight write-only registers R0..R7.
 * ========================================================================== */
static inline void render_scanline_ti99_tms9918a(uint16_t *out, uint8_t y, const uint8_t *vram, const uint8_t *vdp_regs)
{
    bool m2 = (vdp_regs[0] & 0x02) != 0;                                  /* Graphics II */
    uint8_t backdrop = vdp_regs[7] & 0x0F;
    uint16_t border = g_palette_tms9918[backdrop ? backdrop : 1];
    uint16_t name_tab  = (uint16_t)((vdp_regs[2] & 0x0F) << 10);
    uint16_t color_tab = m2 ? (uint16_t)((vdp_regs[3] & 0x80) << 6) : (uint16_t)(vdp_regs[3] << 6);
    uint16_t patt_tab  = m2 ? (uint16_t)((vdp_regs[4] & 0x04) << 11) : (uint16_t)((vdp_regs[4] & 0x07) << 11);
    uint16_t sat_base  = (uint16_t)((vdp_regs[5] & 0x7F) << 7);
    uint16_t spt_base  = (uint16_t)((vdp_regs[6] & 0x07) << 11);
    bool size16 = (vdp_regs[1] & 0x02) != 0, mag = (vdp_regs[1] & 0x01) != 0;
    int spr_h = (size16 ? 16 : 8) << (mag ? 1 : 0);

    fill_line(out, 0, 64, border);
    fill_line(out, 576, 640, border);
    uint16_t *dst = out + 64;                                             /* 256 px doubled = 512 */

    /* backdrop tiles */
    int tile_row = y >> 3, tile_y = y & 7;
    uint16_t third = m2 ? (uint16_t)((y >> 6) * 0x800) : 0;
    for (int tx = 0; tx < 32; tx++) {
        uint8_t name = vram[(name_tab + tile_row * 32 + tx) & 0x3FFF];
        uint8_t patt = vram[(patt_tab + third + name * 8 + tile_y) & 0x3FFF];
        uint8_t col  = m2 ? vram[(color_tab + third + name * 8 + tile_y) & 0x3FFF]
                          : vram[(color_tab + (name >> 3)) & 0x3FFF];
        uint8_t fg = col >> 4, bg = col & 0x0F;
        uint16_t cfg = g_palette_tms9918[fg ? fg : backdrop], cbg = g_palette_tms9918[bg ? bg : backdrop];
        for (int b = 0; b < 8; b++) {
            uint16_t c = (patt >> (7 - b)) & 1 ? cfg : cbg;
            *dst++ = c; *dst++ = c;
        }
    }

    /* sprites: first four on the line are drawn, the fifth sets the status flag */
    uint8_t drawn[256];
    memset(drawn, 0, sizeof drawn);
    int on_line = 0;
    for (int s = 0; s < 32; s++) {
        const uint8_t *a = &vram[(sat_base + s * 4) & 0x3FFF];
        if (a[0] == 0xD0) break;
        int rel = (int)y - (int)((uint8_t)(a[0] + 1));
        if (rel < 0 || rel >= spr_h) continue;
        if (++on_line > 4) { g_beam_sync.tms_vdp_status |= (uint8_t)(0x40 | (s & 0x1F)); break; }
        int sx = (int)a[1] - ((a[3] & 0x80) ? 32 : 0);
        uint8_t color = a[3] & 0x0F;
        int row = mag ? rel >> 1 : rel;
        uint8_t name = size16 ? (a[2] & 0xFC) : a[2];
        for (int half = 0; half < (size16 ? 2 : 1); half++) {
            uint8_t patt = vram[(spt_base + name * 8 + half * 16 + row) & 0x3FFF];
            for (int b = 0; b < 8; b++) {
                if (!((patt >> (7 - b)) & 1)) continue;
                for (int m = 0; m < (mag ? 2 : 1); m++) {
                    int px = sx + ((half * 8 + b) << (mag ? 1 : 0)) + m;
                    if (px < 0 || px >= 256) continue;
                    if (drawn[px]) { g_beam_sync.tms_vdp_status |= 0x20; continue; }
                    drawn[px] = 1;
                    if (color) { uint16_t rgb = g_palette_tms9918[color]; out[64 + px * 2] = rgb; out[65 + px * 2] = rgb; }
                }
            }
        }
    }
}

#endif /* RP2350_HSTX_BEAMRACER_H */
