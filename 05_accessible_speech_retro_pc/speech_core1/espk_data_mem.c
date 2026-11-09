/*
 * espk_data_mem.c -- replacement for third_party/espk/espk/espk_data.c:
 * the ESPK.DAT pack may be supplied as a memory blob (XIP flash on the RP2350,
 * via espk_data_set_blob) or, when no blob is registered, read from a file
 * (host build).  Same symbols as the original so the rest of ESPK is unchanged.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "espk.h"

#define MAGIC "ESPKDAT1"
#define NAMELEN 12
typedef struct { char name[NAMELEN]; uint32_t offset, length; } entry_t;

static const unsigned char *blob;
static long blob_len;
static int n_entries;

void espk_data_set_blob(const void *p, long len) { blob = (const unsigned char *)p; blob_len = len; }

int espk_data_load(const char *file)
{
    if (!blob) {
#ifdef ESPK_NO_FILE_IO
        (void)file; return -1;
#else
        FILE *f = fopen(file, "rb");
        long len;
        unsigned char *m;
        if (!f) return -1;
        fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
        if (len < 16) { fclose(f); return -4; }
        m = (unsigned char *)malloc((size_t)len);
        if (!m) { fclose(f); return -2; }
        if (fread(m, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(m); return -3; }
        fclose(f);
        blob = m; blob_len = len;
#endif
    }
    if (blob_len < 16 || memcmp(blob, MAGIC, 8) != 0) return -4;
    n_entries = blob[8] | (blob[9] << 8);
    return 0;
}

const void *espk_data_get(const char *name, long *len)
{
    const entry_t *e = (const entry_t *)(blob + 16);
    for (int i = 0; i < n_entries; i++) {
        if (strncmp(e[i].name, name, NAMELEN) == 0) {
            if (len) *len = (long)e[i].length;
            return blob + e[i].offset;
        }
    }
    if (len) *len = 0;
    return NULL;
}

long espk_data_size(void) { return blob_len; }

int espk_data_owns(const void *p)
{
    return blob != NULL && (const unsigned char *)p >= blob && (const unsigned char *)p < blob + blob_len;
}
