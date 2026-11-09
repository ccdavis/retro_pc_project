/* espk_data.c - the packed data file ESPK.DAT: phontab, phonindex, phondata,
 * intonations and en_dict in one file (tools/mkdat.py), read into one
 * malloc'd block.  Entries are 16-byte aligned. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "espk.h"

#define MAGIC "ESPKDAT1"
#define NAMELEN 12

typedef struct {
	char name[NAMELEN];
	uint32_t offset, length;
} entry_t;

static unsigned char *blob;
static long blob_len;
static int n_entries;

int espk_data_load(const char *file)
{
	FILE *f = fopen(file, "rb");
	long len;
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len < 16) { fclose(f); return -4; }
	blob = (unsigned char *)malloc(len);
	if (!blob) { fclose(f); return -2; }
	if (fread(blob, 1, len, f) != (size_t)len) { fclose(f); return -3; }
	fclose(f);
	if (memcmp(blob, MAGIC, 8) != 0) return -4;
	n_entries = blob[8] | (blob[9] << 8);
	blob_len = len;
	return 0;
}

const void *espk_data_get(const char *name, long *len)
{
	const entry_t *e = (const entry_t *)(blob + 16);
	int i;
	for (i = 0; i < n_entries; i++) {
		if (strncmp(e[i].name, name, NAMELEN) == 0) {
			if (len) *len = e[i].length;
			return blob + e[i].offset;
		}
	}
	if (len) *len = 0;
	return NULL;
}

long espk_data_size(void) { return blob_len; }

/* 1 if p points into the pack: espeak's dictionary is used there in place and must not be freed */
int espk_data_owns(const void *p)
{
	return blob != NULL && (const unsigned char *)p >= blob && (const unsigned char *)p < blob + blob_len;
}
