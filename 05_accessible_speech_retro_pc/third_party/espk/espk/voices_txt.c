/* voices_txt.c - the two voice files of the en+klatt voice, embedded so that
 * no directory tree is needed on DOS.  voices.c's fopen/fgets/fclose are
 * redirected here (see lib/voices.c, top). */
#include <string.h>
#include <stdio.h>
#include "espk_vfile.h"

static const struct { const char *name; const char *text; } voices[] = {
	/* espeak-ng-data/lang/gmw/en */
	{ "en", "name English (Great Britain)\nlanguage en-gb  2\nlanguage en 2\ntunes s1 c1 q1 e1\n" },
	/* espeak-ng-data/voices/!v/klatt */
	{ "klatt", "language variant\nname klatt\nklatt 1\n" },
	{ NULL, NULL }
};

static espk_vfile_t vf;

FILE *espk_vopen(const char *path)
{
	const char *base = strrchr(path, '/');
	int i;
	base = base ? base + 1 : path;
	for (i = 0; voices[i].name; i++) {
		if (strcmp(voices[i].name, base) == 0) {
			vf.p = voices[i].text;
			return (FILE *)&vf;
		}
	}
	return NULL;
}

char *espk_vgets(char *buf, int n, FILE *f)
{
	espk_vfile_t *v = (espk_vfile_t *)f;
	int i = 0;
	if (*v->p == 0)
		return NULL;
	while (*v->p && i < n - 1) {
		char c = *v->p++;
		buf[i++] = c;
		if (c == '\n')
			break;
	}
	buf[i] = 0;
	return buf;
}

int espk_vclose(FILE *f) { (void)f; return 0; }

/* the voice files exist as far as LoadVoice's path probing is concerned */
int espk_vlength(const char *path)
{
	return espk_vopen(path) ? 1 : -2;
}
