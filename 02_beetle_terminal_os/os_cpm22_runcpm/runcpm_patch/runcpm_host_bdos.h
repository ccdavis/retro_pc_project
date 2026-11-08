/*
 * runcpm_host_bdos.h -- host BDOS extensions 220..223 for RunCPM
 * (MockbaTheBorg/RunCPM, MIT).  Included from cpm.h by runcpm_cpm_h.patch.
 *
 *   220 (DCh) SSH     DE -> command tail ("user@host [port]")
 *   221 (DDh) TELNET  DE -> command tail ("host [port]")
 *   222 (DEh) WIFI    DE -> command tail ("ssid password" or empty for status)
 *   223 (DFh) BANK    DE -> command tail ("0".."7" or empty for status)
 *   224 (E0h) SPEAK   DE -> NUL-terminated text for the project-5 speech core
 *                        (doubletalk_write_string: control codes pass through)
 *
 * The tail is a CP/M command tail: length byte, then the characters.  Each
 * function is a weak default here (prints a message through RunCPM's console)
 * so the posix build keeps working; the Beetle sketch overrides them with the
 * LibSSH-ESP32 / WiFiClient implementations.  Return value goes to A.
 */
#ifndef RUNCPM_HOST_BDOS_H
#define RUNCPM_HOST_BDOS_H

#define BDOS_HOST_SSH     220
#define BDOS_HOST_TELNET  221
#define BDOS_HOST_WIFI    222
#define BDOS_HOST_BANK    223
#define BDOS_HOST_SPEAK   224

/* copy the CP/M command tail at Z80 address 'de' into a C string (max 127) */
static void host_bdos_tail(uint16 de, char *out, int outsz)
{
    int n = _RamRead(de), i;
    if (n > outsz - 1) n = outsz - 1;
    for (i = 0; i < n; i++) out[i] = (char)_RamRead((uint16)(de + 1 + i));
    out[n] = 0;
    /* trim leading blanks */
    i = 0; while (out[i] == ' ') i++;
    if (i) memmove(out, out + i, strlen(out + i) + 1);
}

__attribute__((weak)) uint8 host_bdos_ssh(const char *tail)    { _puts("SSH: no host network on this build: "); _puts(tail); _puts("\r\n"); return 1; }
__attribute__((weak)) uint8 host_bdos_telnet(const char *tail) { _puts("TELNET: no host network on this build: "); _puts(tail); _puts("\r\n"); return 1; }
__attribute__((weak)) uint8 host_bdos_wifi(const char *tail)   { _puts("WIFI: no host network on this build: "); _puts(tail); _puts("\r\n"); return 1; }
__attribute__((weak)) uint8 host_bdos_bank(const char *tail)   { _puts("BANK: no MMU on this build: "); _puts(tail); _puts("\r\n"); return 1; }
__attribute__((weak)) uint8 host_bdos_speak(const char *text)  { _puts("SPEAK: no synthesizer on this build: "); _puts(text); _puts("\r\n"); return 1; }

/* copy a NUL-terminated string at Z80 address 'de' (max 255 bytes); CR shown as space */
static void host_bdos_cstring(uint16 de, char *out, int outsz)
{
    int i;
    for (i = 0; i < outsz - 1; i++) {
        uint8 c = _RamRead((uint16)(de + i));
        if (c == 0) break;
        out[i] = (c == '\r' || c == '\n') ? ' ' : (char)c;
    }
    out[i] = 0;
}

/* called from _Bdos() for C = 220..223; returns 1 when handled */
static int host_bdos_dispatch(uint8 fn, uint16 de, uint8 *result)
{
    char tail[256];
    if (fn < BDOS_HOST_SSH || fn > BDOS_HOST_SPEAK) return 0;
    if (fn == BDOS_HOST_SPEAK) { host_bdos_cstring(de, tail, sizeof tail); *result = host_bdos_speak(tail); return 1; }
    host_bdos_tail(de, tail, sizeof tail);
    switch (fn) {
        case BDOS_HOST_SSH:    *result = host_bdos_ssh(tail); break;
        case BDOS_HOST_TELNET: *result = host_bdos_telnet(tail); break;
        case BDOS_HOST_WIFI:   *result = host_bdos_wifi(tail); break;
        default:               *result = host_bdos_bank(tail); break;
    }
    return 1;
}
#endif
