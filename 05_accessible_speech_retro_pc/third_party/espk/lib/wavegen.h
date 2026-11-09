/* slim replacement for espeak-ng's wavegen.h: what the frontend calls into the
 * output stage (implemented in espk/wgout.c) */
#ifndef ESPEAK_NG_WAVEGEN_H
#define ESPEAK_NG_WAVEGEN_H
#include "voice.h"
int GetAmplitude(void);
void InitBreath(void);
void SetPitch2(voice_t *voice, int pitch1, int pitch2, int *pitch_base, int *pitch_range);
void WavegenInit(int rate, int wavemult_fact);
void WavegenFini(void);
int WavegenFill(void);
void WavegenSetVoice(voice_t *v);
int WcmdqFree(void);
void WcmdqStop(void);
int WcmdqUsed(void);
void WcmdqInc(void);
#endif
