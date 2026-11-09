/* dictionary compiler is not part of the ESPK subset; these are only used by
 * the phoneme trace output and are stubbed in espk/stubs.c */
#ifndef ESPEAK_NG_COMPILEDICT_H
#define ESPEAK_NG_COMPILEDICT_H
char *DecodeRule(const char *group_chars, int group_length, char *rule, int control, char *output);
void print_dictionary_flags(unsigned int *flags, char *buf, int buf_len);
#endif
