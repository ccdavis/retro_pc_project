/* stubs.c - what the ESPK subset leaves out: sound icons, SSML, markers,
 * the dictionary compiler's rule decoder (used only by phoneme traces), the
 * MBROLA name, and the data path. */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>
#include "speech.h"
#include "soundicon.h"
#include "ssml.h"
#include "compiledict.h"
#include "mbrola.h"

char path_home[N_PATH_BUF] = "";
char mbrola_name[20] = "";
int n_soundicon_tab = 0;
SOUND_ICON soundicon_tab[N_SOUNDICON_TAB];

const int param_defaults[N_SPEECH_PARAM] = {
	0,   // silence (internal use)
	espeakRATE_NORMAL, // rate wpm
	100, // volume
	50,  // pitch
	50,  // range
	0,   // punctuation
	0,   // capital letters
	0,   // wordgap
	0,   // options
	0,   // intonation
	100, // ssml break mul
	0,
	0,   // emphasis
	0,   // line length
	0,   // voice type
};

int LookupSoundicon(int c) { (void)c; return -1; }
int LoadSoundFile2(const char *fname) { (void)fname; return -1; }

int ProcessSsmlTag(wchar_t *xml_buf, char *outbuf, int *outix, int n_outbuf, const char *xmlbase,
                   bool *audio_text, char *current_voice_id, espeak_VOICE *base_voice,
                   char *base_voice_variant_name, bool *ignore_text, bool *clear_skipping_text,
                   int *sayas_mode, int *sayas_start, SSML_STACK *ssml_stack, int *n_ssml_stack,
                   int *n_param_stack, int *speech_parameters)
{
	(void)xml_buf; (void)outbuf; (void)outix; (void)n_outbuf; (void)xmlbase; (void)audio_text;
	(void)current_voice_id; (void)base_voice; (void)base_voice_variant_name; (void)ignore_text;
	(void)clear_skipping_text; (void)sayas_mode; (void)sayas_start; (void)ssml_stack;
	(void)n_ssml_stack; (void)n_param_stack; (void)speech_parameters;
	return 0;
}
int ParseSsmlReference(char *ref, int *c1, int *c2) { (void)ref; (void)c1; (void)c2; return 0; }

void MarkerEvent(int type, unsigned int char_position, int value, int value2, unsigned char *out_ptr)
{ (void)type; (void)char_position; (void)value; (void)value2; (void)out_ptr; }

char *DecodeRule(const char *group_chars, int group_length, char *rule, int control, char *output)
{ (void)group_chars; (void)group_length; (void)rule; (void)control; output[0] = 0; return output; }
void print_dictionary_flags(unsigned int *flags, char *buf, int buf_len)
{ (void)flags; if (buf_len > 0) buf[0] = 0; }

/* output hooks (phoneme alignment callbacks) are not used */
espeak_ng_OUTPUT_HOOKS *output_hooks = NULL;
