/* dirent.h stand-in for bare-metal newlib builds of ESPK.  espeak-ng's voices.c
 * scans a voice directory in espeak_ListVoices(); ESPK loads its two voice files
 * from embedded strings and never calls it, but the code must compile. */
#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H
typedef struct { int unused; } DIR;
struct dirent { char d_name[256]; };
static inline DIR *opendir(const char *path) { (void)path; return (DIR *)0; }
static inline struct dirent *readdir(DIR *d) { (void)d; return (struct dirent *)0; }
static inline int closedir(DIR *d) { (void)d; return 0; }
#endif
