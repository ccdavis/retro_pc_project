/**
 * beetle_cpm22_ssh.ino -- OS Option B: a CP/M-style host shell with SSH, Telnet,
 * Wi-Fi and bank commands.
 *
 * WHAT THIS IS: a command processor that looks like the CP/M 2.2 CCP (A> prompt,
 * DIR, TYPE, ERA, PIP, drive letters A:, B:, M:), keeps a 64 KB Z80 address space
 * plus an expanded RAM pool, and executes the four generated utilities
 * (SSH.COM, TELNET.COM, WIFI.COM, BANK.COM) with a tiny interpreter that knows
 * only the eleven Z80 opcodes they use.
 *
 * WHAT THIS IS NOT: a Z80 emulator or a CP/M BDOS.  WordStar, Turbo Pascal and
 * MBASIC will not run here.  For real CP/M use RunCPM with the extensions in
 * runcpm_patch/ (see runcpm_patch/README.md).  The first draft's comments and
 * PLAN.md described this file as a 192 KB CP/M; that was not true.
 *
 * Memory: the Arduino core exposes 320 KB of DRAM to the application on both chips;
 * Wi-Fi, BLE and an SSH session need about 165 KB of it at run time, so the 64 KB
 * Z80 space plus CPM_EXPANDED_BANKS x 16 KB is limited to 2 banks (32 KB) on the
 * C6 and 1 bank on the C3, not the 8 banks (128 KB) of the first draft.  BANK
 * prints the free heap.
 */
#define VIDEO_BACKEND_COMPOSITE_SPI     1
#define VIDEO_BACKEND_VGA_FABGL         2
#define VIDEO_BACKEND_UART_RP2350_HDMI  3
#ifndef SELECTED_VIDEO_BACKEND
#define SELECTED_VIDEO_BACKEND          VIDEO_BACKEND_UART_RP2350_HDMI
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include "libssh_esp32.h"
#include <libssh/libssh.h>

/* Expanded RAM: 16 KB banks.  The Arduino core gives the application 320 KB of DRAM
 * on both the C6 and the C3; Wi-Fi + BLE take ~110 KB at run time and an SSH
 * session ~55 KB.  Compiled sizes (arduino-esp32 3.3.12): this sketch's other
 * globals are ~45 KB, the Z80 space 64 KB, each bank 16 KB.  Two banks on the C6
 * leave ~140 KB of heap, one bank on the C3 ~155 KB.  (Must come after Arduino.h:
 * CONFIG_IDF_TARGET_* is defined by sdkconfig.h, which Arduino.h includes.) */
#ifndef CPM_EXPANDED_BANKS
#  if defined(CONFIG_IDF_TARGET_ESP32C3)
#    define CPM_EXPANDED_BANKS 1
#  else
#    define CPM_EXPANDED_BANKS 2
#  endif
#endif
#define MMU_NUM_BANKS CPM_EXPANDED_BANKS

#if SELECTED_VIDEO_BACKEND == VIDEO_BACKEND_COMPOSITE_SPI
  #include "../video_backends/video_composite_spi.h"
#elif SELECTED_VIDEO_BACKEND == VIDEO_BACKEND_VGA_FABGL
  #include "../video_backends/video_vga_fabgl.h"
#else
  #include "../video_backends/video_uart_rp2350_hdmi.h"
#endif
#include "ssh_com_bdos_ext.h"

static uint8_t       s_z80Ram[65536];
static char          s_curDrive = 'A';
static Preferences   s_prefs;
static QueueHandle_t s_keyQueue;
static String        s_bleStatus = "Scanning for BLE keyboard...";

/* ---- console input: BLE keyboard (ASCII via the same translator as Option A) + Serial */
static void enqueueKeyChar(char c) { if (s_keyQueue) xQueueSend(s_keyQueue, &c, 0); }
static void enqueueKeySeq(const char *s) { while (*s) enqueueKeyChar(*s++); }

static bool pollConsoleChar(char *out, TickType_t wait = 0)
{
    if (Serial.available()) { *out = (char)Serial.read(); return true; }
    return s_keyQueue && xQueueReceive(s_keyQueue, out, wait) == pdTRUE;
}

static String readConsoleLine(bool echo = true)
{
    String line;
    for (;;) {
        char c;
        if (!pollConsoleChar(&c, pdMS_TO_TICKS(10))) continue;
        if (c == '\r' || c == '\n') { video_write("\r\n"); return line; }
        if (c == '\b' || c == 0x7F) { if (line.length()) { line.remove(line.length() - 1); if (echo) video_write("\b \b"); } }
        else if (c >= 0x20 && c <= 0x7E) { line += c; if (echo) video_putc(c); }
    }
}

static uint8_t s_prevKeys[6];
static void translateHidReport(const uint8_t *rpt)
{
    uint8_t mod = rpt[0];
    bool shift = mod & 0x22, ctrl = mod & 0x11;
    for (int i = 2; i < 8; i++) {
        uint8_t k = rpt[i];
        if (!k) continue;
        bool held = false;
        for (int j = 0; j < 6; j++) if (s_prevKeys[j] == k) held = true;
        if (held) continue;
        if (k == 0x52) { enqueueKeySeq("\x1B[A"); continue; }
        if (k == 0x51) { enqueueKeySeq("\x1B[B"); continue; }
        if (k == 0x4F) { enqueueKeySeq("\x1B[C"); continue; }
        if (k == 0x50) { enqueueKeySeq("\x1B[D"); continue; }
        if (k == 0x28) { enqueueKeyChar('\r'); continue; }
        if (k == 0x29) { enqueueKeyChar(0x1B); continue; }
        if (k == 0x2A) { enqueueKeyChar(0x7F); continue; }
        if (k == 0x2B) { enqueueKeyChar('\t'); continue; }
        if (k == 0x2C) { enqueueKeyChar(' '); continue; }
        if (k >= 0x04 && k <= 0x1D) { enqueueKeyChar(ctrl ? (char)(k - 0x03) : (char)((shift ? 'A' : 'a') + k - 0x04)); continue; }
        static const char num[] = "1234567890", numS[] = "!@#$%^&*()", pun[] = "-=[]\\#;'`,./", punS[] = "_+{}|~:\"~<>?";
        if (k >= 0x1E && k <= 0x27) { enqueueKeyChar(shift ? numS[k - 0x1E] : num[k - 0x1E]); continue; }
        if (k >= 0x2D && k <= 0x38) { char c = shift ? punS[k - 0x2D] : pun[k - 0x2D]; if (ctrl && c == ']') c = 0x1D; enqueueKeyChar(c); }
    }
    memcpy(s_prevKeys, rpt + 2, 6);
}

/* ---- BLE HID host (NimBLE-Arduino 2.x) ------------------------------------------ */
static NimBLEAddress s_bleAddr;
static volatile bool s_bleNeedConnect;
static const NimBLEUUID HID_SVC_UUID((uint16_t)0x1812);
static void bleNotifyCB(NimBLERemoteCharacteristic *, uint8_t *p, size_t len, bool) { if (len == 9) translateHidReport(p + 1); else if (len >= 8) translateHidReport(p); }
static void startScan() { NimBLEDevice::getScan()->start(0, false, true); }
class BleScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override
    { if (dev->isAdvertisingService(HID_SVC_UUID)) { s_bleAddr = dev->getAddress(); NimBLEDevice::getScan()->stop(); s_bleNeedConnect = true; } }
    void onScanEnd(const NimBLEScanResults &, int) override { if (!s_bleNeedConnect) startScan(); }
} s_scanCB;
class BleClientCB : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient *, int) override { s_bleStatus = "Disconnected, rescanning"; memset(s_prevKeys, 0, 6); startScan(); }
} s_clientCB;
static void bleConnect()
{
    NimBLEClient *c = NimBLEDevice::getClientByPeerAddress(s_bleAddr);
    if (!c) { c = NimBLEDevice::createClient(); if (!c) return; c->setClientCallbacks(&s_clientCB, false); }
    if (!c->connect(s_bleAddr) || !c->secureConnection()) { startScan(); return; }
    NimBLERemoteService *svc = c->getService(HID_SVC_UUID);
    int n = 0;
    if (svc) for (auto *ch : svc->getCharacteristics(true)) if (ch->canNotify() && ch->subscribe(true, bleNotifyCB)) n++;
    if (!n) { c->disconnect(); startScan(); return; }
    s_bleStatus = "Connected (" + String(s_bleAddr.toString().c_str()) + ")";
}

/* ---- host BDOS 220..223 --------------------------------------------------------- */
static void cpm_bdos_ssh_handler(uint16_t dma)
{
    /* CP/M's CCP upper-cases the command tail; hosts are case-insensitive, user names
     * usually are not, so the tail is lower-cased: type the user name in lower case. */
    String tail = cpm_read_cmd_tail(s_z80Ram, dma, true);
    int sp = tail.indexOf(' ');
    String userHost = sp == -1 ? tail : tail.substring(0, sp);
    unsigned port = sp == -1 ? 22u : (unsigned)tail.substring(sp + 1).toInt();
    int at = userHost.indexOf('@');
    if (at <= 0) { video_write("Usage: A>SSH user@host [port]\r\n"); return; }
    String user = userHost.substring(0, at), host = userHost.substring(at + 1);
    if (WiFi.status() != WL_CONNECTED) { video_write("No Wi-Fi. A>WIFI <ssid> <password>\r\n"); return; }
    video_write(("SSH: connecting to " + host + ":" + String(port) + " as " + user + "...\r\n").c_str());

    ssh_session s = ssh_new();
    if (!s) return;
    ssh_options_set(s, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(s, SSH_OPTIONS_USER, user.c_str());
    ssh_options_set(s, SSH_OPTIONS_PORT, &port);
    if (ssh_connect(s) != SSH_OK) { video_write(("SSH: " + String(ssh_get_error(s)) + "\r\n").c_str()); ssh_free(s); return; }
    video_write("Password: ");
    String pass = readConsoleLine(false);
    if (ssh_userauth_password(s, nullptr, pass.c_str()) != SSH_AUTH_SUCCESS) { video_write("Access denied.\r\n"); ssh_disconnect(s); ssh_free(s); return; }
    ssh_channel ch = ssh_channel_new(s);
    if (ch && ssh_channel_open_session(ch) == SSH_OK && ssh_channel_request_pty_size(ch, "vt100", 80, 25) == SSH_OK && ssh_channel_request_shell(ch) == SSH_OK) {
        char buf[256];
        while (ssh_channel_is_open(ch) && !ssh_channel_is_eof(ch)) {
            int n = ssh_channel_read_nonblocking(ch, buf, sizeof buf, 0);
            if (n > 0) video_write_buf((const uint8_t *)buf, n); else if (n < 0) break;
            char k;
            while (pollConsoleChar(&k, 0)) ssh_channel_write(ch, &k, 1);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    if (ch) { ssh_channel_close(ch); ssh_channel_free(ch); }
    ssh_disconnect(s); ssh_free(s);
    video_write("\r\n[Back to the A> shell]\r\n");
}

static void cpm_bdos_telnet_handler(uint16_t dma)
{
    String tail = cpm_read_cmd_tail(s_z80Ram, dma, true);
    int sp = tail.indexOf(' ');
    String host = sp == -1 ? tail : tail.substring(0, sp);
    uint16_t port = sp == -1 ? 23 : (uint16_t)tail.substring(sp + 1).toInt();
    if (!host.length()) { video_write("Usage: A>TELNET host [port]\r\n"); return; }
    WiFiClient c;
    video_write(("TELNET: " + host + ":" + String(port) + " (Ctrl+] exits)\r\n").c_str());
    if (!c.connect(host.c_str(), port)) { video_write("No carrier.\r\n"); return; }
    while (c.connected()) {
        while (c.available()) video_putc((char)c.read());
        char k;
        while (pollConsoleChar(&k, 0)) { if (k == 0x1D) { c.stop(); break; } c.write((uint8_t)k); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    video_write("\r\n[Back to the A> shell]\r\n");
}

static void cpm_bdos_wifi_handler(uint16_t dma)
{
    String tail = cpm_read_cmd_tail(s_z80Ram, dma, false);
    if (!tail.length()) {
        video_write(WiFi.status() == WL_CONNECTED ? ("WIFI: connected to " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")\r\n").c_str()
                                                  : "WIFI: disconnected. A>WIFI <ssid> <pass>\r\n");
        return;
    }
    int sp = tail.indexOf(' ');
    String ssid = sp == -1 ? tail : tail.substring(0, sp), pass = sp == -1 ? "" : tail.substring(sp + 1);
    video_write(("WIFI: joining " + ssid + " (the CCP upper-cased it; SSIDs are case-sensitive)...\r\n").c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) { delay(500); video_putc('.'); }
    if (WiFi.status() == WL_CONNECTED) { s_prefs.putString("ssid", ssid); s_prefs.putString("pass", pass); video_write(("\r\nIP " + WiFi.localIP().toString() + "\r\n").c_str()); }
    else video_write("\r\nWi-Fi connection failed.\r\n");
}

static void cpm_bdos_bank_handler(uint16_t dma)
{
    String tail = cpm_read_cmd_tail(s_z80Ram, dma, false);
    if (tail.length() && isDigit(tail[0])) cpm_mmu_select_bank(s_z80Ram, (uint8_t)(tail.toInt() % MMU_NUM_BANKS));
    char msg[200];
    snprintf(msg, sizeof msg, "Bank window 8000-BFFF: bank %u of %u (%u KB pool)\r\nRAM disk B:/M: free %u KB, host heap free %u KB\r\n",
             (unsigned)s_activeMmuBank, (unsigned)MMU_NUM_BANKS, (unsigned)(MMU_NUM_BANKS * 16), (unsigned)(ramdisk_free_bytes() / 1024), (unsigned)(ESP.getFreeHeap() / 1024));
    video_write(msg);
}

static bool cpm_handle_custom_bdos(uint8_t c, uint16_t de)
{
    switch (c) {
        case BDOS_HOST_SSH:    cpm_bdos_ssh_handler(de);    return true;
        case BDOS_HOST_TELNET: cpm_bdos_telnet_handler(de); return true;
        case BDOS_HOST_WIFI:   cpm_bdos_wifi_handler(de);   return true;
        case BDOS_HOST_BANK:   cpm_bdos_bank_handler(de);   return true;
        default: return false;
    }
}

/* ---- the utility interpreter: eleven opcodes, enough for the generated .COM files --- */
static bool cpm_execute_tpa_at_0100()
{
    uint16_t pc = 0x0100, de = 0x0080;
    uint8_t a = 0, c = 0;
    bool zf = true;
    for (int guard = 0; guard < 100000 && pc != 0x0000; guard++) {
        uint8_t op = s_z80Ram[pc++];
        switch (op) {
            case 0x11: de = s_z80Ram[pc] | (s_z80Ram[pc + 1] << 8); pc += 2; break;                 /* LD DE,nn */
            case 0x3E: a = s_z80Ram[pc++]; zf = a == 0; break;                                        /* LD A,n */
            case 0x3A: { uint16_t ad = s_z80Ram[pc] | (s_z80Ram[pc + 1] << 8); pc += 2; a = s_z80Ram[ad]; break; } /* LD A,(nn) */
            case 0xB7: zf = a == 0; break;                                                            /* OR A */
            case 0x20: { int8_t r = (int8_t)s_z80Ram[pc++]; if (!zf) pc = (uint16_t)(pc + r); break; } /* JR NZ */
            case 0x28: { int8_t r = (int8_t)s_z80Ram[pc++]; if (zf) pc = (uint16_t)(pc + r); break; }  /* JR Z */
            case 0xC8: if (zf) return true; break;                                                    /* RET Z */
            case 0x0E: c = s_z80Ram[pc++]; break;                                                     /* LD C,n */
            case 0xD3: { uint8_t port = s_z80Ram[pc++]; if (port == MMU_IO_PORT) cpm_mmu_select_bank(s_z80Ram, a); break; }
            case 0xDB: { uint8_t port = s_z80Ram[pc++]; if (port == MMU_IO_PORT) a = s_activeMmuBank; break; }
            case 0xCD: {
                uint16_t t = s_z80Ram[pc] | (s_z80Ram[pc + 1] << 8); pc += 2;
                if (t == 0x0005 && !cpm_handle_custom_bdos(c, de)) {
                    if (c == 0x09) { while (s_z80Ram[de] != '$') video_putc((char)s_z80Ram[de++]); }
                    else if (c == 0x02) video_putc((char)(de & 0xFF));
                }
                break;
            }
            case 0xC3: pc = s_z80Ram[pc] | (s_z80Ram[pc + 1] << 8); break;                            /* JP nn */
            case 0xC9: case 0x76: return true;                                                        /* RET / HALT */
            default:
                video_write("This program needs a real Z80 (RunCPM): see runcpm_patch/README.md\r\n");
                return false;
        }
    }
    return true;
}

/* ---- CCP-like command processor ------------------------------------------------- */
static bool isRamDiskDrive(char d) { return d == 'B' || d == 'M'; }

static void cpm_cmd_pip(String args)
{
    args.trim(); args.toUpperCase();
    if (!(args.startsWith("B:=") || args.startsWith("M:="))) { video_write("Usage: A>PIP B:=*.*  or  A>PIP B:=FILE.OVR\r\n"); return; }
    String spec = args.substring(3);
    if (spec.startsWith("A:")) spec = spec.substring(2);
    File dir = LittleFS.open("/A0");
    int copied = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = String(f.name()); name.toUpperCase();
        if (spec != "*.*" && spec != name) continue;
        size_t sz = f.size();
        uint8_t *tmp = (uint8_t *)malloc(sz);
        if (!tmp) { video_write("Out of host memory\r\n"); continue; }
        f.read(tmp, sz);
        if (ramdisk_write_file(name, tmp, sz)) { video_write(("PIP: " + name + " -> B:\r\n").c_str()); copied++; }
        else video_write(("PIP: B: full, " + name + " not copied\r\n").c_str());
        free(tmp);
    }
    if (!copied) video_write("NO FILE\r\n");
}

static void dir_entry(char drive, const String &nameIn, int &col)
{
    String name = nameIn; name.toUpperCase();
    int dot = name.lastIndexOf('.');
    String base = dot == -1 ? name : name.substring(0, dot), ext = dot == -1 ? "" : name.substring(dot + 1);
    char e[24];
    snprintf(e, sizeof e, "%c: %-8s %-3s   ", drive, base.c_str(), ext.c_str());
    video_write(e);
    if (++col >= 4) { video_write("\r\n"); col = 0; }
}

static void cpm_process_ccp_line(String line)
{
    line.trim();
    if (!line.length()) return;
    if (line.length() == 2 && line[1] == ':') {
        char d = toupper(line[0]);
        if (d == 'A' || d == 'B' || d == 'M') s_curDrive = d; else video_write(("BDOS ERR ON " + String(d) + ": SELECT\r\n").c_str());
        return;
    }
    int sp = line.indexOf(' ');
    String verb = sp == -1 ? line : line.substring(0, sp), tail = sp == -1 ? "" : line.substring(sp);
    verb.toUpperCase();
    String tailUpper = tail; tailUpper.toUpperCase();          /* the CCP upper-cases the tail */

    memset(&s_z80Ram[0x0080], 0, 128);
    uint8_t tailLen = (uint8_t)min((int)tailUpper.length(), 126);
    s_z80Ram[0x0080] = tailLen;
    memcpy(&s_z80Ram[0x0081], tailUpper.c_str(), tailLen);

    if (verb == "DIR") {
        int col = 0;
        if (isRamDiskDrive(s_curDrive)) { for (int i = 0; i < RAMDISK_MAX_FILES; i++) if (s_ramDiskDir[i].used) dir_entry(s_curDrive, s_ramDiskDir[i].name, col); }
        else { File dir = LittleFS.open("/A0"); for (File f = dir.openNextFile(); f; f = dir.openNextFile()) dir_entry('A', f.name(), col); }
        if (col) video_write("\r\n");
        return;
    }
    if (verb == "PIP") { cpm_cmd_pip(tail); return; }
    if (verb == "TYPE") {
        tail.trim(); tail.toUpperCase();
        if (isRamDiskDrive(s_curDrive)) {
            int slot = ramdisk_find_file(tail);
            if (slot < 0) { video_write("NO FILE\r\n"); return; }
            const uint8_t *p = &s_expandedSram[0][0] + s_ramDiskDir[slot].offset;
            for (uint32_t i = 0; i < s_ramDiskDir[slot].size && p[i] != 0x1A; i++) video_putc((char)p[i]);
        } else {
            File f = LittleFS.open("/A0/" + tail, "r");
            if (!f) { video_write("NO FILE\r\n"); return; }
            while (f.available()) { char ch = (char)f.read(); if (ch == 0x1A) break; video_putc(ch); }
            f.close();
        }
        video_write("\r\n");
        return;
    }
    if (verb == "ERA") {
        tail.trim(); tail.toUpperCase();
        bool ok = isRamDiskDrive(s_curDrive) ? ramdisk_delete_file(tail) : LittleFS.remove("/A0/" + tail);
        if (!ok) video_write("NO FILE\r\n");
        return;
    }
    if (verb == "CLS") { video_write("\x1B[2J\x1B[H"); return; }

    String com = verb + ".COM";
    int32_t loaded = -1;
    if (isRamDiskDrive(s_curDrive)) loaded = ramdisk_read_file(com, &s_z80Ram[0x0100], 0xFD00);
    if (loaded < 0) { File f = LittleFS.open("/A0/" + com, "r"); if (f) { loaded = (int32_t)f.read(&s_z80Ram[0x0100], 0xFD00); f.close(); } }
    if (loaded <= 0) { video_write((verb + "?\r\n").c_str()); return; }
    cpm_execute_tpa_at_0100();
}

void setup()
{
    Serial.begin(115200);
    s_keyQueue = xQueueCreate(128, sizeof(char));
    s_z80Ram[0x0005] = 0xC3; s_z80Ram[0x0006] = 0x00; s_z80Ram[0x0007] = 0xFE;   /* JP FE00: BDOS vector, 63.5 KB TPA */

    video_init();
    LittleFS.begin(true);
    cpm_provision_network_com_files();
    libssh_begin();

    NimBLEDevice::init("AirTag-CPM");
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCB, false);
    scan->setActiveScan(true);
    startScan();

    WiFi.mode(WIFI_STA);
    s_prefs.begin("airtag-net", false);
    String ssid = s_prefs.getString("ssid", ""), pass = s_prefs.getString("pass", "");
    if (ssid.length()) WiFi.begin(ssid.c_str(), pass.c_str());

    char banner[160];
    snprintf(banner, sizeof banner, "\x1B[2J\x1B[HAirTag CP/M-style shell: A: flash, B:/M: %u KB RAM disk, SSH/TELNET/WIFI/BANK\r\n(not a Z80 emulator: see runcpm_patch/README.md for real CP/M)\r\n\r\n", (unsigned)(MMU_NUM_BANKS * 16));
    video_write(banner);
}

void loop()
{
    if (s_bleNeedConnect) { s_bleNeedConnect = false; bleConnect(); }
    char prompt[4] = { s_curDrive, '>', 0, 0 };
    video_write(prompt);
    cpm_process_ccp_line(readConsoleLine(true));
}
