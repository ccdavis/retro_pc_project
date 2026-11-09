#ifndef ESPK_VFILE_H
#define ESPK_VFILE_H
#include <stdio.h>
typedef struct { const char *p; } espk_vfile_t;
FILE *espk_vopen(const char *path);
char *espk_vgets(char *buf, int n, FILE *f);
int espk_vclose(FILE *f);
int espk_vlength(const char *path);
#endif
