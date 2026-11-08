/*
 * tdsr_hypervisor_reader.h
 *
 * Out-of-band hypervisor screen reader for RP2350 Core 1, modelled on rust-tdsr
 * (/home/ccd/rust-tdsr: review cursor, symbol dictionary, key echo detection,
 * TUI screen diffing) and on how Provox 7 polls text video memory from a timer.
 *
 * It never touches the emulated machine: Core 1 snapshots the emulated video
 * RAM in shared SRAM (a VT100 cell grid for CP/M, the Apple //e $0400 text
 * pages, or CGA $B8000), normalises it to an 80-column character/attribute
 * grid, diffs it against the previous snapshot every 30 ms and speaks through
 * core1_say().  Zero bytes of emulated RAM and zero emulated CPU cycles.
 *
 * All functions run on Core 1 (main loop or the speech core's idle hook).
 * Key events reach it through doubletalk_key_pop(); Core 0 only decides
 * whether a key is a reader hotkey (doubletalk_key_event) and queues it.
 */
#ifndef TDSR_HYPERVISOR_READER_H
#define TDSR_HYPERVISOR_READER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_COLS 80
#define SR_ROWS 30                     /* room for the 80x30 HDMI card; 25 for CGA/CP M, 24 Apple */

#define SR_ATTR_INVERSE  0x01          /* highlighted: inverse video / distinct background */
#define SR_ATTR_BOLD     0x02
#define SR_ATTR_BLINK    0x04

#define SR_DIFF_PERIOD_MS   30
#define SR_ECHO_WINDOW_MS   400        /* a cursor-row change this soon after a key is its echo */
#define SR_DOUBLE_TAP_MS    400
#define SR_REPAINT_ROWS     4          /* more changed rows than this, not console-shaped: repaint */

typedef enum {
    SR_PUNCT_NONE = 0,                 /* letters, digits, a few symbols */
    SR_PUNCT_SOME = 1,                 /* coding / math symbols spoken */
    SR_PUNCT_ALL  = 2                  /* every symbol spoken */
} sr_punct_level_t;

/* Same layout as TermCell in 02_beetle_terminal_os/video_backends/rp2350_hdmi_vt100_card.c */
typedef struct {
    char    ch;
    uint8_t fg;                        /* ANSI 16-colour index */
    uint8_t bg;
} tdsr_termcell_t;

typedef struct {
    uint8_t  curr_ch[SR_ROWS][SR_COLS];
    uint8_t  curr_attr[SR_ROWS][SR_COLS];
    uint8_t  prev_ch[SR_ROWS][SR_COLS];
    uint8_t  prev_attr[SR_ROWS][SR_COLS];
    int      rows, cols;               /* active size of the captured screen */

    int      hw_x, hw_y;               /* emulated hardware cursor */
    int      prev_hw_x, prev_hw_y;
    int      rev_x, rev_y;             /* review cursor */

    /* key echo and double-tap state */
    uint8_t  last_alt_cmd;
    uint32_t last_alt_ms;
    uint8_t  last_key;
    uint32_t last_key_ms;
    bool     have_key;

    uint8_t  row_hist[SR_ROWS];        /* change history bits, newest in bit 0 (volatile rows) */
    uint32_t last_diff_ms;
    bool     have_prev;

    /* settings */
    sr_punct_level_t punct_level;
    bool     key_echo;
    bool     cursor_tracking;
    bool     com2_provox_active;       /* a DOS screen reader took over the synthesizer port */

    /* statistics (tests and diagnostics) */
    uint32_t spoken_lines, spoken_highlights, suppressed_echo, scrolls, repaints, silent_ticks;
} tdsr_state_t;

extern tdsr_state_t g_sr;

void tdsr_init(void);

/* VRAM adapters: snapshot the emulated screen into g_sr.curr_* */
void tdsr_capture_termcells(const tdsr_termcell_t *cells, int rows, int cols, int cursor_x, int cursor_y);
void tdsr_capture_apple2(const uint8_t *main_ram_64k, const uint8_t *aux_ram_64k,
                         bool col80, bool altcharset, int cursor_x, int cursor_y);
void tdsr_capture_cga(const uint8_t *b8000, int cursor_x, int cursor_y);

/* Call often (idle hook / main loop): pops key events, runs the diff every
 * SR_DIFF_PERIOD_MS.  The caller must have captured the screen first. */
void tdsr_service(uint32_t now_ms);

/* Building blocks, exposed for the tests */
void tdsr_diff_now(uint32_t now_ms);
void tdsr_handle_key(uint8_t ascii, uint8_t mods, uint32_t now_ms);
int  tdsr_format_text(const uint8_t *raw, int len, bool all_punct, char *out, int out_size);
const char *tdsr_symbol_name(uint8_t c);
bool tdsr_is_hotkey(uint8_t ascii, uint8_t mods);

#ifdef __cplusplus
}
#endif
#endif /* TDSR_HYPERVISOR_READER_H */
