/* slim replacement for espeak-ng's speech.h (ESPK subset) */
#ifndef ESPEAK_NG_SPEECH_H
#define ESPEAK_NG_SPEECH_H
#include <espeak-ng/espeak_ng.h>
#include <stdlib.h>
#define MAKE_MEM_UNDEFINED(addr, len) ((void) ((void) addr, len))
#define PLATFORM_POSIX 1
#define PATHSEP  '/'
#define __cdecl
#define N_PATH_BUF 256
#define NAME_MAX 255
#define PATH_ESPEAK_DATA "/espeak-ng-data"
extern char path_home[N_PATH_BUF];
extern const int param_defaults[N_SPEECH_PARAM];
extern int saved_parameters[N_SPEECH_PARAM];
/* markers are not used: no-op (stubs.c) */
void MarkerEvent(int type, unsigned int char_position, int value, int value2, unsigned char *out_ptr);
#endif
