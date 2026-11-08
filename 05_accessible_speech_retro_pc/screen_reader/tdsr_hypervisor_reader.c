/*
 * tdsr_hypervisor_reader.c -- see tdsr_hypervisor_reader.h.
 *
 * Diff policy (rust-tdsr tui/mod.rs and performer.rs, reduced to what a VRAM
 * snapshot can know; there is no print stream here):
 *   1. If a DOS screen reader drives the synthesizer port, stay silent.
 *   2. Undo a scroll: find the shift k that makes most rows of the previous
 *      frame line up with the current one; rows that still differ are new.
 *   3. A newly highlighted span (inverse video / distinct background) is a
 *      menu bar item or a spreadsheet cell: speak it, nothing else.
 *   4. Changed rows that lie between the old and new cursor row are console
 *      output: speak them in order.  The row the cursor is on is skipped when
 *      the change is the echo of a key typed in the last 400 ms (the key was
 *      already echoed when pressed) or when the row is volatile (a clock).
 *   5. Many changed rows anywhere else is a full-screen repaint: silent,
 *      the review cursor snaps to the hardware cursor (Alt+I reads it).
 *   6. Cursor moved to another row with no text change: speak the new line.
 *      Moved along the row: speak the character under it.
 */
#include <string.h>
#include <stdio.h>
#include "tdsr_hypervisor_reader.h"
#include "../speech_core1/doubletalk_core1.h"

tdsr_state_t g_sr;

/* ------------------------------------------------------------------------ */
/* Symbols (rust-tdsr symbols: "bang", "tick", "line" for underscore...)     */
/* ------------------------------------------------------------------------ */
const char *tdsr_symbol_name(uint8_t c)
{
    switch (c) {
        case ' ':  return "space";      case '!':  return "bang";
        case '"':  return "quote";      case '#':  return "hash";
        case '$':  return "dollar";     case '%':  return "percent";
        case '&':  return "and";        case '\'': return "tick";
        case '(':  return "left paren"; case ')':  return "right paren";
        case '*':  return "star";       case '+':  return "plus";
        case ',':  return "comma";      case '-':  return "dash";
        case '.':  return "dot";        case '/':  return "slash";
        case ':':  return "colon";      case ';':  return "semi";
        case '<':  return "less";       case '=':  return "equals";
        case '>':  return "greater";    case '?':  return "question";
        case '@':  return "at";         case '[':  return "left bracket";
        case '\\': return "backslash";  case ']':  return "right bracket";
        case '^':  return "caret";      case '_':  return "line";
        case '`':  return "grave";      case '{':  return "left brace";
        case '|':  return "bar";        case '}':  return "right brace";
        case '~':  return "tilde";
        default:   return NULL;
    }
}

static const char *const k_nato[26] = {
    "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India",
    "Juliet", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo",
    "Sierra", "Tango", "Uniform", "Victor", "Whiskey", "X-ray", "Yankee", "Zulu"
};

static bool is_some_punct(uint8_t c) { return strchr("#$%&*+<=>@[\\]^_{|}~", (int)c) != NULL && c != 0; }
static bool is_word_char(uint8_t c)  { return c > ' ' && c < 0x7F; }

static int append(char *out, int pos, int size, const char *s)
{
    while (*s && pos < size - 1) out[pos++] = *s++;
    out[pos] = 0;
    return pos;
}

/* Expand symbols per punctuation level, condense runs of repeated symbols
 * ("=====" -> "5 equals"), trim trailing blanks.  Returns the output length. */
int tdsr_format_text(const uint8_t *raw, int len, bool all_punct, char *out, int size)
{
    int pos = 0, i;
    while (len > 0 && raw[len - 1] <= ' ') len--;          /* trailing blanks */
    out[0] = 0;
    for (i = 0; i < len && pos < size - 24; ) {
        uint8_t c = raw[i];
        const char *sym = (c == ' ') ? NULL : tdsr_symbol_name(c);
        bool expand = sym && (all_punct || g_sr.punct_level == SR_PUNCT_ALL ||
                              (g_sr.punct_level == SR_PUNCT_SOME && is_some_punct(c)));
        int run = 1;
        while (sym && i + run < len && raw[i + run] == c) run++;
        if (sym && run >= 3) {                              /* "4 equals" like rust-tdsr */
            char num[16];
            snprintf(num, sizeof num, "%s%d %s ", (pos && out[pos - 1] != ' ') ? " " : "", run, sym);
            pos = append(out, pos, size, num);
            i += run;
            continue;
        }
        if (expand) {
            pos = append(out, pos, size, (pos && out[pos - 1] != ' ') ? " " : "");
            pos = append(out, pos, size, sym);
            pos = append(out, pos, size, " ");
        } else {
            char o = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
            if (o != ' ' || pos == 0 || out[pos - 1] != ' ') { out[pos++] = o; out[pos] = 0; }   /* no double blanks after an expanded symbol */
        }
        i++;
    }
    while (pos > 0 && out[pos - 1] == ' ') out[--pos] = 0;
    return pos;
}

static void say_text(const uint8_t *raw, int len, bool all_punct, bool flush)
{
    char out[DT_SAY_MAX];
    if (tdsr_format_text(raw, len, all_punct, out, sizeof out) == 0) {
        core1_say("blank", flush);
    } else {
        core1_say(out, flush);
    }
}

static void say_char(uint8_t c, bool phonetic, bool flush)
{
    char buf[48];
    const char *sym;
    if (c >= 'A' && c <= 'Z') {
        if (phonetic) snprintf(buf, sizeof buf, "cap %s", k_nato[c - 'A']);
        else          snprintf(buf, sizeof buf, "cap %c", c);
        core1_say(buf, flush);
        return;
    }
    if (c >= 'a' && c <= 'z') {
        if (phonetic) core1_say(k_nato[c - 'a'], flush);
        else { buf[0] = (char)c; buf[1] = 0; core1_say(buf, flush); }
        return;
    }
    sym = tdsr_symbol_name(c);
    if (sym) { core1_say(sym, flush); return; }
    if (c > ' ' && c < 0x7F) { buf[0] = (char)c; buf[1] = 0; core1_say(buf, flush); }
    else core1_say("blank", flush);
}

/* ------------------------------------------------------------------------ */
/* Init                                                                      */
/* ------------------------------------------------------------------------ */
void tdsr_init(void)
{
    memset(&g_sr, 0, sizeof g_sr);
    memset(g_sr.curr_ch, ' ', sizeof g_sr.curr_ch);
    memset(g_sr.prev_ch, ' ', sizeof g_sr.prev_ch);
    g_sr.rows = 25;
    g_sr.cols = SR_COLS;
    g_sr.punct_level = SR_PUNCT_SOME;
    g_sr.key_echo = true;
    g_sr.cursor_tracking = true;
}

/* ------------------------------------------------------------------------ */
/* VRAM adapters                                                             */
/* ------------------------------------------------------------------------ */
static void clear_rest(int from_row)
{
    for (int r = from_row; r < SR_ROWS; r++) {
        memset(g_sr.curr_ch[r], ' ', SR_COLS);
        memset(g_sr.curr_attr[r], 0, SR_COLS);
    }
}

static void set_cursor(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= g_sr.cols) x = g_sr.cols - 1;
    if (y < 0) y = 0;
    if (y >= g_sr.rows) y = g_sr.rows - 1;
    g_sr.hw_x = x; g_sr.hw_y = y;
}

/* A cell is highlighted when its background differs from the row's dominant
 * background (rust-tdsr: "distinct background on its row"), which covers both
 * a bar on a plain row and a selected item inside a bar. */
static void mark_distinct_background(const uint8_t *bg_row, uint8_t *attr_row, int cols)
{
    int count[16] = {0}, best = 0;
    for (int c = 0; c < cols; c++) count[bg_row[c] & 15]++;
    for (int i = 1; i < 16; i++) if (count[i] > count[best]) best = i;
    for (int c = 0; c < cols; c++)
        if ((bg_row[c] & 15) != best) attr_row[c] |= SR_ATTR_INVERSE;
}

void tdsr_capture_termcells(const tdsr_termcell_t *cells, int rows, int cols, int cx, int cy)
{
    uint8_t bg[SR_COLS];
    if (rows > SR_ROWS) rows = SR_ROWS;
    if (cols > SR_COLS) cols = SR_COLS;
    g_sr.rows = rows; g_sr.cols = cols;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const tdsr_termcell_t *t = &cells[r * cols + c];
            uint8_t ch = (uint8_t)t->ch;
            g_sr.curr_ch[r][c]   = (ch >= 0x20 && ch < 0x7F) ? ch : ' ';
            g_sr.curr_attr[r][c] = 0;
            bg[c] = t->bg;
        }
        for (int c = cols; c < SR_COLS; c++) { g_sr.curr_ch[r][c] = ' '; g_sr.curr_attr[r][c] = 0; }
        mark_distinct_background(bg, g_sr.curr_attr[r], cols);
    }
    clear_rest(rows);
    set_cursor(cx, cy);
}

/* Apple //e text: 24 rows of 40 (or 80, even columns from auxiliary RAM) at
 * $0400-$07FF, rows interleaved in three groups of eight.  Character codes:
 *   $00-$3F inverse (upper case / symbols), $40-$7F flashing (or, with
 *   ALTCHARSET, $40-$5F MouseText and $60-$7F inverse lower case),
 *   $80-$BF normal upper case / symbols, $C0-$FF normal upper / lower case.
 * The flashing range maps to glyphs $00-$3F: a flashing space is $60, not '`'. */
void tdsr_capture_apple2(const uint8_t *main_ram, const uint8_t *aux_ram, bool col80, bool altcharset, int cx, int cy)
{
    static const uint16_t row_base[24] = {
        0x0400, 0x0480, 0x0500, 0x0580, 0x0600, 0x0680, 0x0700, 0x0780,
        0x0428, 0x04A8, 0x0528, 0x05A8, 0x0628, 0x06A8, 0x0728, 0x07A8,
        0x0450, 0x04D0, 0x0550, 0x05D0, 0x0650, 0x06D0, 0x0750, 0x07D0
    };
    int cols = col80 ? 80 : 40;
    g_sr.rows = 24; g_sr.cols = cols;
    for (int r = 0; r < 24; r++) {
        uint16_t base = row_base[r];
        for (int c = 0; c < SR_COLS; c++) {
            uint8_t raw, ch, attr = 0;
            if (c >= cols) { g_sr.curr_ch[r][c] = ' '; g_sr.curr_attr[r][c] = 0; continue; }
            if (col80) raw = (c & 1) ? main_ram[base + (c >> 1)] : aux_ram[base + (c >> 1)];
            else       raw = main_ram[base + c];
            if (raw >= 0x80) {
                ch = raw & 0x7F;                            /* normal */
                if (ch < 0x20) ch += 0x40;                  /* $80-$9F: upper case */
            } else if (raw < 0x40) {
                ch = raw; attr = SR_ATTR_INVERSE;           /* inverse */
                if (ch < 0x20) ch += 0x40;
            } else if (altcharset) {
                if (raw < 0x60) { ch = ' '; attr = SR_ATTR_INVERSE; }   /* MouseText glyph: decoration */
                else            { ch = raw; attr = SR_ATTR_INVERSE; }   /* inverse lower case */
            } else {
                ch = raw & 0x3F; attr = SR_ATTR_BLINK;      /* flashing: glyphs of the inverse set */
                if (ch < 0x20) ch += 0x40;
            }
            g_sr.curr_ch[r][c]   = ch;
            g_sr.curr_attr[r][c] = attr;
        }
    }
    clear_rest(24);
    set_cursor(cx, cy);
}

/* IBM PC CGA/MDA text page: character, attribute pairs.  Bit 7 of the
 * attribute is blink (or bright background), never a highlight by itself. */
void tdsr_capture_cga(const uint8_t *b8000, int cx, int cy)
{
    uint8_t bg[SR_COLS];
    g_sr.rows = 25; g_sr.cols = 80;
    for (int r = 0; r < 25; r++) {
        for (int c = 0; c < 80; c++) {
            uint8_t ch = b8000[(r * 80 + c) * 2];
            uint8_t at = b8000[(r * 80 + c) * 2 + 1];
            g_sr.curr_ch[r][c]   = (ch >= 0x20 && ch < 0x7F) ? ch : ' ';
            g_sr.curr_attr[r][c] = ((at & 0x0F) == 0x0F || (at & 0x0F) == 0x09) ? SR_ATTR_BOLD : 0;
            bg[c] = (at >> 4) & 0x07;
        }
        mark_distinct_background(bg, g_sr.curr_attr[r], 80);
    }
    clear_rest(25);
    set_cursor(cx, cy);
}

/* ------------------------------------------------------------------------ */
/* Review cursor commands                                                    */
/* ------------------------------------------------------------------------ */
bool tdsr_is_hotkey(uint8_t ascii, uint8_t mods)
{
    uint8_t c = (ascii >= 'a' && ascii <= 'z') ? (uint8_t)(ascii - 32) : ascii;
    if (!(mods & DT_MOD_ALT)) return false;
    if ((mods & DT_MOD_SHIFT) && (ascii == 0x08 || ascii == 0x7F)) return true;
    return c != 0 && strchr("UIOJKLM,.<>CRSHX=+-", (int)c) != NULL;
}

static void read_review_line(bool all_punct)
{
    say_text(g_sr.curr_ch[g_sr.rev_y], g_sr.cols, all_punct, true);
}

static void word_bounds(const uint8_t *line, int x, int *start, int *end)
{
    while (x < g_sr.cols && !is_word_char(line[x])) x++;
    if (x >= g_sr.cols) { *start = *end = g_sr.cols; return; }
    *start = x; while (*start > 0 && is_word_char(line[*start - 1])) (*start)--;
    *end = x;   while (*end < g_sr.cols && is_word_char(line[*end])) (*end)++;
}

static void read_review_word(bool spell)
{
    const uint8_t *line = g_sr.curr_ch[g_sr.rev_y];
    int start, end;
    word_bounds(line, g_sr.rev_x, &start, &end);
    if (start >= g_sr.cols) { core1_say("blank", true); return; }
    if (!spell) { say_text(&line[start], end - start, false, true); return; }
    {
        char buf[DT_SAY_MAX];
        int pos = 0;
        for (int i = start; i < end && pos < (int)sizeof buf - 24; i++) {
            uint8_t c = line[i];
            const char *sym = tdsr_symbol_name(c);
            if (c >= 'A' && c <= 'Z') pos += snprintf(buf + pos, sizeof buf - pos, "cap %c, ", c);
            else if (sym) pos += snprintf(buf + pos, sizeof buf - pos, "%s, ", sym);
            else pos += snprintf(buf + pos, sizeof buf - pos, "%c, ", c);
        }
        core1_say(buf, true);
    }
}

static void announce_edge(const char *what) { core1_say(what, true); }

void tdsr_handle_key(uint8_t ascii, uint8_t mods, uint32_t now_ms)
{
    uint8_t cmd;
    bool double_tap, shift = (mods & DT_MOD_SHIFT) != 0;

    if (!(mods & DT_MOD_ALT)) {
        /* An ordinary key.  Core 0 already flushed speech.  Echo it, and remember
         * it so the diff can recognise its echo on the cursor row. */
        g_sr.last_key = ascii;
        g_sr.last_key_ms = now_ms;
        g_sr.have_key = true;
        if (!g_sr.key_echo) return;
        if (ascii == 0x08 || ascii == 0x7F) {            /* backspace: say what is being deleted */
            if (g_sr.hw_x > 0) say_char(g_sr.curr_ch[g_sr.hw_y][g_sr.hw_x - 1], false, false);
        } else if (ascii >= 0x20 && ascii < 0x7F) {
            say_char(ascii, false, false);
        }
        return;
    }

    if (shift && (ascii == 0x08 || ascii == 0x7F)) {     /* panic chord: Alt+Shift+Backspace */
        char msg[96];
        g_dt_state.speed_0_9 = 5; g_dt_state.pitch_0_99 = 50; g_dt_state.volume_0_9 = 5;
        g_sr.punct_level = SR_PUNCT_SOME;
        g_sr.rev_x = g_sr.hw_x; g_sr.rev_y = g_sr.hw_y;
        snprintf(msg, sizeof msg, "Speech reset. Rate %d words per minute. Cursor row %d column %d.",
                 dt_speed_to_dectalk_wpm(5), g_sr.hw_y + 1, g_sr.hw_x + 1);
        core1_say(msg, true);
        return;
    }

    cmd = (ascii >= 'a' && ascii <= 'z') ? (uint8_t)(ascii - 32) : ascii;
    double_tap = (cmd == g_sr.last_alt_cmd) && ((uint32_t)(now_ms - g_sr.last_alt_ms) < SR_DOUBLE_TAP_MS);
    g_sr.last_alt_cmd = cmd;
    g_sr.last_alt_ms  = now_ms;

    switch (cmd) {
        /* lines: Alt+U previous, Alt+I current (twice: all punctuation), Alt+O next;
         * with Shift: top / bottom of screen */
        case 'U':
            if (shift) { g_sr.rev_y = 0; read_review_line(false); return; }
            if (g_sr.rev_y == 0) { announce_edge("top"); return; }
            g_sr.rev_y--; read_review_line(false); return;
        case 'I':
            read_review_line(double_tap); return;
        case 'O':
            if (shift) { g_sr.rev_y = g_sr.rows - 1; read_review_line(false); return; }
            if (g_sr.rev_y >= g_sr.rows - 1) { announce_edge("bottom"); return; }
            g_sr.rev_y++; read_review_line(false); return;

        /* words: Alt+J previous, Alt+K current (twice: spell), Alt+L next */
        case 'J': {
            const uint8_t *line = g_sr.curr_ch[g_sr.rev_y];
            int x = g_sr.rev_x;
            while (x > 0 && is_word_char(line[x])) x--;
            while (x > 0 && !is_word_char(line[x])) x--;
            while (x > 0 && is_word_char(line[x - 1])) x--;
            if (x == g_sr.rev_x && !is_word_char(line[x])) { announce_edge("start of line"); return; }
            g_sr.rev_x = x; read_review_word(false); return;
        }
        case 'K':
            read_review_word(double_tap); return;
        case 'L': {
            const uint8_t *line = g_sr.curr_ch[g_sr.rev_y];
            int x = g_sr.rev_x;
            while (x < g_sr.cols && is_word_char(line[x])) x++;
            while (x < g_sr.cols && !is_word_char(line[x])) x++;
            if (x >= g_sr.cols) { announce_edge("end of line"); return; }
            g_sr.rev_x = x; read_review_word(false); return;
        }

        /* characters: Alt+M previous, Alt+, current (twice: phonetic), Alt+. next;
         * Shift+M start of line, Shift+. (>) end of line */
        case 'M':
            if (shift) { g_sr.rev_x = 0; say_char(g_sr.curr_ch[g_sr.rev_y][0], false, true); return; }
            if (g_sr.rev_x == 0) { announce_edge("start of line"); return; }
            g_sr.rev_x--; say_char(g_sr.curr_ch[g_sr.rev_y][g_sr.rev_x], false, true); return;
        case ',': case '<':
            say_char(g_sr.curr_ch[g_sr.rev_y][g_sr.rev_x], double_tap, true); return;
        case '.': case '>':
            if (shift || cmd == '>') {
                int x = g_sr.cols - 1;
                while (x > 0 && g_sr.curr_ch[g_sr.rev_y][x] == ' ') x--;
                g_sr.rev_x = x; say_char(g_sr.curr_ch[g_sr.rev_y][x], false, true); return;
            }
            if (g_sr.rev_x >= g_sr.cols - 1) { announce_edge("end of line"); return; }
            g_sr.rev_x++; say_char(g_sr.curr_ch[g_sr.rev_y][g_sr.rev_x], false, true); return;

        case 'C': {                                      /* cursor position, review snaps to it */
            char msg[80];
            bool inv = (g_sr.curr_attr[g_sr.hw_y][g_sr.hw_x] & SR_ATTR_INVERSE) != 0;
            g_sr.rev_x = g_sr.hw_x; g_sr.rev_y = g_sr.hw_y;
            snprintf(msg, sizeof msg, "Row %d, column %d%s", g_sr.hw_y + 1, g_sr.hw_x + 1, inv ? ", highlighted" : "");
            core1_say(msg, true); return;
        }
        case 'R': {                                      /* punctuation level */
            static const char *const names[3] = { "Punctuation none", "Punctuation some", "Punctuation all" };
            g_sr.punct_level = (sr_punct_level_t)((g_sr.punct_level + 1) % 3);
            core1_say(names[g_sr.punct_level], true); return;
        }
        case 'S': {                                      /* next available engine */
            static const char *const names[SYNTH_ENGINE_COUNT] = { "DECtalk voice", "E speak Klatt voice", "Test tone voice" };
            int e = g_dt_state.engine;
            for (int i = 0; i < SYNTH_ENGINE_COUNT; i++) {
                e = (e + 1) % SYNTH_ENGINE_COUNT;
                if (core1_engine_available((synth_engine_id_t)e)) break;
            }
            g_dt_state.engine = (uint8_t)e;
            core1_say(names[e], true); return;
        }
        case 'X':                                        /* key echo on/off */
            g_sr.key_echo = !g_sr.key_echo;
            core1_say(g_sr.key_echo ? "Key echo on" : "Key echo off", true); return;
        case '=': case '+':
            if (g_dt_state.speed_0_9 < 9) g_dt_state.speed_0_9++;
            core1_say("Faster", true); return;
        case '-':
            if (g_dt_state.speed_0_9 > 0) g_dt_state.speed_0_9--;
            core1_say("Slower", true); return;
        case 'H':
            core1_say("Screen reader keys. Alt U, I, O: previous, current, next line; Alt I twice reads punctuation. "
                      "Alt J, K, L: words; Alt K twice spells. Alt M, comma, period: characters; Alt comma twice is phonetic. "
                      "Shift with U, O, M, period: top, bottom, start, end. Alt C: cursor position. Alt R: punctuation level. "
                      "Alt S: switch voice. Alt X: key echo. Alt plus and minus: speed. Alt Shift Backspace: reset speech.", true);
            return;
        default:
            return;
    }
}

/* ------------------------------------------------------------------------ */
/* Screen diff                                                               */
/* ------------------------------------------------------------------------ */
static bool row_eq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, SR_COLS) == 0; }

/* shift k that lines up most rows of prev with curr (prev[r+k] == curr[r]); 0 if none */
static int detect_scroll(void)
{
    int best_k = 0, best_hits = 0, rows = g_sr.rows, hits0 = 0;
    for (int r = 0; r < rows; r++) if (row_eq(g_sr.curr_ch[r], g_sr.prev_ch[r])) hits0++;
    for (int k = 1; k <= rows / 2; k++) {
        int hits = 0;
        for (int r = 0; r + k < rows; r++) if (row_eq(g_sr.curr_ch[r], g_sr.prev_ch[r + k])) hits++;
        if (hits > best_hits) { best_hits = hits; best_k = k; }
    }
    /* a real scroll lines up most of the surviving rows, lines up MORE rows than no
     * shift does (a screen of identical lines is not a scroll), and was not blank */
    if (best_k && best_hits > hits0 && best_hits >= (rows - best_k) * 3 / 4 && best_hits >= 3) {
        int nonblank = 0;
        for (int r = 0; r + best_k < g_sr.rows; r++) {
            const uint8_t *row = g_sr.prev_ch[r + best_k];
            for (int c = 0; c < SR_COLS; c++) if (row[c] != ' ') { nonblank++; break; }
        }
        return nonblank >= 2 ? best_k : 0;
    }
    return 0;
}

/* The new inverse span: present now, absent (or different text / place) before.
 * Prefer the one on the cursor row, else the shortest (menu items are short,
 * title bars are long).  Returns row or -1; fills span start/end. */
static int find_new_highlight(int *out_s, int *out_e)
{
    int best_r = -1, best_len = 0x7FFF;
    int total_inv = 0;
    for (int r = 0; r < g_sr.rows; r++) {
        int c = 0;
        while (c < g_sr.cols) {
            int s, e, i; bool same;
            while (c < g_sr.cols && !(g_sr.curr_attr[r][c] & SR_ATTR_INVERSE)) c++;
            if (c >= g_sr.cols) break;
            s = c;
            while (c < g_sr.cols && (g_sr.curr_attr[r][c] & SR_ATTR_INVERSE)) c++;
            e = c;
            total_inv += e - s;
            /* trim blanks */
            while (s < e && g_sr.curr_ch[r][s] == ' ') s++;
            while (e > s && g_sr.curr_ch[r][e - 1] == ' ') e--;
            if (s >= e) continue;
            same = true;
            for (i = s; i < e; i++)
                if (!(g_sr.prev_attr[r][i] & SR_ATTR_INVERSE) || g_sr.prev_ch[r][i] != g_sr.curr_ch[r][i]) { same = false; break; }
            if (same) continue;
            if (r == g_sr.hw_y && best_r != g_sr.hw_y) { best_r = r; best_len = e - s; *out_s = s; *out_e = e; }
            else if (best_r != g_sr.hw_y && e - s < best_len) { best_r = r; best_len = e - s; *out_s = s; *out_e = e; }
        }
    }
    if (total_inv > g_sr.rows * g_sr.cols / 2) return -1;      /* inverse screen: not a highlight */
    return best_r;
}

static void snapshot(void)
{
    memcpy(g_sr.prev_ch, g_sr.curr_ch, sizeof g_sr.curr_ch);
    memcpy(g_sr.prev_attr, g_sr.curr_attr, sizeof g_sr.curr_attr);
    g_sr.prev_hw_x = g_sr.hw_x;
    g_sr.prev_hw_y = g_sr.hw_y;
    g_sr.have_prev = true;
}

static bool is_echo_change(int row, int change_lo, int change_hi, uint32_t now_ms)
{
    int lo, hi;
    if (row != g_sr.hw_y || !g_sr.have_key) return false;
    if ((uint32_t)(now_ms - g_sr.last_key_ms) > SR_ECHO_WINDOW_MS) return false;
    lo = g_sr.prev_hw_x < g_sr.hw_x ? g_sr.prev_hw_x : g_sr.hw_x;
    hi = g_sr.prev_hw_x > g_sr.hw_x ? g_sr.prev_hw_x : g_sr.hw_x;
    if (lo > 0) lo--;                                  /* a cell before: backspace / overwrite */
    return change_lo >= lo && change_hi <= hi + 1;
}

void tdsr_diff_now(uint32_t now_ms)
{
    int changed[SR_ROWS], nchanged = 0, k, rows = g_sr.rows;
    int hs = 0, he = 0, hrow;
    bool cursor_rows_only;

    if (g_sr.com2_provox_active) { snapshot(); return; }
    if (!g_sr.have_prev) { snapshot(); return; }

    /* 2. undo a scroll */
    k = detect_scroll();
    if (k) {
        g_sr.scrolls++;
        memmove(g_sr.prev_ch[0], g_sr.prev_ch[k], (size_t)(rows - k) * SR_COLS);
        memmove(g_sr.prev_attr[0], g_sr.prev_attr[k], (size_t)(rows - k) * SR_COLS);
        for (int r = rows - k; r < rows; r++) { memset(g_sr.prev_ch[r], ' ', SR_COLS); memset(g_sr.prev_attr[r], 0, SR_COLS); }
        g_sr.prev_hw_y -= k;
        if (g_sr.prev_hw_y < 0) g_sr.prev_hw_y = 0;
    }
    for (int r = 0; r < rows; r++) {
        bool diff = !row_eq(g_sr.curr_ch[r], g_sr.prev_ch[r]) || memcmp(g_sr.curr_attr[r], g_sr.prev_attr[r], SR_COLS) != 0;
        g_sr.row_hist[r] = (uint8_t)((g_sr.row_hist[r] << 1) | (diff ? 1 : 0));
        if (diff) changed[nchanged++] = r;
    }

    /* 3. a new highlight */
    hrow = find_new_highlight(&hs, &he);
    if (hrow >= 0) {
        g_sr.spoken_highlights++;
        g_sr.rev_y = hrow; g_sr.rev_x = hs;
        say_text(&g_sr.curr_ch[hrow][hs], he - hs, false, true);
        snapshot();
        return;
    }

    if (nchanged == 0) {
        /* 6. cursor movement alone */
        if (g_sr.cursor_tracking && g_sr.hw_y != g_sr.prev_hw_y) {
            g_sr.rev_y = g_sr.hw_y; g_sr.rev_x = g_sr.hw_x;
            g_sr.spoken_lines++;
            say_text(g_sr.curr_ch[g_sr.hw_y], g_sr.cols, false, true);
        } else if (g_sr.cursor_tracking && g_sr.hw_x != g_sr.prev_hw_x &&
                   !(g_sr.have_key && (uint32_t)(now_ms - g_sr.last_key_ms) <= SR_ECHO_WINDOW_MS &&
                     g_sr.last_key >= 0x20 && g_sr.last_key < 0x7F)) {
            g_sr.rev_y = g_sr.hw_y; g_sr.rev_x = g_sr.hw_x;
            say_char(g_sr.curr_ch[g_sr.hw_y][g_sr.hw_x], false, true);
        }
        snapshot();
        return;
    }

    /* 4. console output: the cursor moved down (or stayed) and every changed row lies
     * between its old and new row.  An upward jump is cursor addressing (TUI). */
    {
        int lo = g_sr.prev_hw_y, hi = g_sr.hw_y;
        cursor_rows_only = g_sr.hw_y >= g_sr.prev_hw_y;
        for (int i = 0; i < nchanged; i++) if (changed[i] < lo || changed[i] > hi) { cursor_rows_only = false; break; }
    }
    if (cursor_rows_only || nchanged <= SR_REPAINT_ROWS) {
        bool spoke = false;
        for (int i = 0; i < nchanged; i++) {
            int r = changed[i], c0 = 0, c1 = SR_COLS;
            while (c0 < SR_COLS && g_sr.curr_ch[r][c0] == g_sr.prev_ch[r][c0] && g_sr.curr_attr[r][c0] == g_sr.prev_attr[r][c0]) c0++;
            while (c1 > c0 && g_sr.curr_ch[r][c1 - 1] == g_sr.prev_ch[r][c1 - 1] && g_sr.curr_attr[r][c1 - 1] == g_sr.prev_attr[r][c1 - 1]) c1--;
            if (is_echo_change(r, c0, c1, now_ms)) { g_sr.suppressed_echo++; continue; }
            if ((g_sr.row_hist[r] & 0x0F) == 0x0F && !cursor_rows_only) continue;   /* volatile row (clock) */
            {
                /* trailing blanks only (a cleared tail) say nothing */
                int len = SR_COLS;
                while (len > 0 && g_sr.curr_ch[r][len - 1] == ' ') len--;
                if (len == 0 && r != g_sr.hw_y) continue;
            }
            g_sr.spoken_lines++;
            say_text(g_sr.curr_ch[r], g_sr.cols, false, false);
            spoke = true;
        }
        if (spoke && g_sr.cursor_tracking) { g_sr.rev_y = g_sr.hw_y; g_sr.rev_x = g_sr.hw_x; }
        snapshot();
        return;
    }

    /* 5. full-screen repaint: stay quiet, snap the review cursor */
    g_sr.repaints++;
    g_sr.silent_ticks++;
    g_sr.rev_y = g_sr.hw_y; g_sr.rev_x = g_sr.hw_x;
    snapshot();
}

void tdsr_service(uint32_t now_ms)
{
    uint8_t a, m;
    while (doubletalk_key_pop(&a, &m)) tdsr_handle_key(a, m, now_ms);
    if ((uint32_t)(now_ms - g_sr.last_diff_ms) >= SR_DIFF_PERIOD_MS) {
        g_sr.last_diff_ms = now_ms;
        tdsr_diff_now(now_ms);
    }
}
