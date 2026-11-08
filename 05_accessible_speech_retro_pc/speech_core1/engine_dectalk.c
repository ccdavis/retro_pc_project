/*
 * engine_dectalk.c -- DECtalk 4.x ARM7/Epson single-threaded engine on Core 1.
 *
 * Built against the tree that vintage-pc-speech / accessible_os builds for DOS:
 * github.com/dectalk/dectalk at 69ebb459 with patches/upstream.diff, defines
 *   -DARM7 -DENGLISH_US -DACNA -DARM7_NOSWI -DENGLISH -Di386 -DWIN32_TEST
 *   -DBLD_DECTALK_DLL -DCHEESY_DICT_COMPRESSION
 * and the full US dictionary (build-gen/maindict.c).  CHEESY_DICT_COMPRESSION is
 * not optional: without it every dictionary lookup misreads the entries and
 * every word gets its own pitch accent (accessible_os/freedos/src/dectalk/NOTES.md).
 *
 * The callback contract (vtm/vtmiont.c OutputData): the engine memcpy's one
 * block of 71 (11025 Hz) or 51 (8000 Hz) samples into the buffer we gave
 * TextToSpeechStart, then calls callback(buffer, 0); code 3 is an index mark
 * with no audio.  The block size is implicit; the second argument is NOT a
 * sample count.  Whatever pointer the callback returns becomes the next output
 * buffer.  Returning NULL sets "halting", which re-initialises the engine, and
 * the next TextToSpeechStart then loops forever in ls_util_next_item_new; so
 * on a flush we keep returning our buffer and simply discard the samples
 * (dtalkd/engine_dectalk.c, reproduced with `dtsay -a`).
 *
 * This file must stay its own translation unit: include/port.h does
 * "#define volatile" to nothing for ARM7 builds.
 */
#include <string.h>
#include "doubletalk_core1.h"      /* before epsonapi.h: keep its volatile qualifiers intact */
#include "epsonapi.h"

static int   s_blksize = 71;
static int   s_fmt     = WAVE_FORMAT_1M16;
static short s_blk[71];
static int   s_discard = 0;
static int   s_ready   = 0;

unsigned long g_dectalk_blocks = 0, g_dectalk_index_marks = 0, g_dectalk_resets = 0;

static short *dectalk_cb(short *b, long code)
{
    if (code == 3) { g_dectalk_index_marks++; return b; }     /* index mark, no audio */
    g_dectalk_blocks++;
    if (!s_discard && !synth_push_pcm16(b, s_blksize))
        s_discard = 1;                                         /* flushed: drain the rest silently */
    return b;                                                  /* never NULL */
}

static void speak_raw(char *text)
{
    int r;
    s_discard = 0;
    r = TextToSpeechStart(text, s_blk, s_fmt);
    if (r == ERR_RESET) g_dectalk_resets++;
    s_discard = 0;
}

/* dtalkd's warm-up: the pipeline once with the output discarded, so every lazily
 * allocated table exists before normal use (the compiled files do call malloc on
 * some text paths).  ~55 M instructions, 370 ms at 150 MHz; the speech core runs
 * it right after the boot message is in the ring (DT_POST_BOOT_WARMUP). */
void engine_dectalk_warmup(void)
{
    static char t1[] = "[:ra 200][:dv ap 122] Warm up: 1,234.5 dollars; the 3rd e-mail from Mr. O'Neil at 10:30 AM (test) & 50% done? Yes!";
    static char t2[] = "[:ra 280][:dv ap 183] Twenty-two 7 9 2026 x y z. ";
    s_discard = 1;
    TextToSpeechStart(t1, s_blk, s_fmt);
    TextToSpeechStart(t2, s_blk, s_fmt);
    s_discard = 0;
}

int engine_dectalk_init(int sample_rate)
{
    s_blksize = (sample_rate == 8000) ? 51 : 71;
    s_fmt     = (sample_rate == 8000) ? WAVE_FORMAT_08M16 : WAVE_FORMAT_1M16;
    if (TextToSpeechInit(dectalk_cb, NULL) != ERR_NOERROR) return -1;
    s_ready = 1;
    return 0;
}

void engine_dectalk_speak(const char *text)
{
    static char buf[DT_SAY_MAX + 64];
    if (!s_ready) return;
    strncpy(buf, text, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    speak_raw(buf);
}
