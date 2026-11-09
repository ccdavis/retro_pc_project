/* sound icons are not supported in the ESPK subset: LookupSoundicon() is a stub */
#ifndef ESPEAK_NG_SOUNDICON_H
#define ESPEAK_NG_SOUNDICON_H
int LookupSoundicon(int c);
int LoadSoundFile2(const char *fname);
typedef struct {
        int name;
        int length;
        char *data;
        char *filename;
} SOUND_ICON;
#define N_SOUNDICON_TAB  4
extern int n_soundicon_tab;
extern SOUND_ICON soundicon_tab[N_SOUNDICON_TAB];
#endif
