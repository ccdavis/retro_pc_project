/**
 * beetle_rtos_ssh_terminal.ino -- OS Option A: FreeRTOS micro-shell with an SSH-2
 * and Telnet client, BLE keyboard input, LittleFS files.
 *
 * Boards: Beetle ESP32-C6 (DFR1117, 512 KB SRAM) or Beetle ESP32-C3 (DFR0868,
 *         400 KB SRAM); both 4 MB flash, 25 x 20.5 mm.
 * Core:   arduino-esp32 3.x.  Libraries: NimBLE-Arduino 2.x, LibSSH-ESP32 (5.x).
 *
 * Video backend (SELECTED_VIDEO_BACKEND):
 *   3 = UART to the RP2350-Zero HDMI VT100 card (default, the only one that works
 *       on these boards without extra hardware design);
 *   1 = composite stub (does not produce video, see video_composite_spi.h);
 *   2 = FabGL VGA, classic dual-core ESP32 only (35 x 34 mm board, core 2.0.x).
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

#if SELECTED_VIDEO_BACKEND == VIDEO_BACKEND_COMPOSITE_SPI
  #include "../video_backends/video_composite_spi.h"
#elif SELECTED_VIDEO_BACKEND == VIDEO_BACKEND_VGA_FABGL
  #include "../video_backends/video_vga_fabgl.h"
#else
  #include "../video_backends/video_uart_rp2350_hdmi.h"
#endif

/* ---- keyboard FIFO (BLE notifications and the USB serial monitor) ------------- */
static QueueHandle_t s_keyQueue;
static String s_bleStatus = "Scanning for BLE keyboard...";

static void enqueueKeyChar(char c) { if (s_keyQueue) xQueueSend(s_keyQueue, &c, 0); }
static void enqueueKeySeq(const char *s) { while (*s) enqueueKeyChar(*s++); }

static bool pollKeyChar(char *out, TickType_t wait = 0)
{
    if (Serial.available()) { *out = (char)Serial.read(); return true; }
    return s_keyQueue && xQueueReceive(s_keyQueue, out, wait) == pdTRUE;
}

static String readLine(bool echo = true)
{
    String line;
    for (;;) {
        char c;
        if (!pollKeyChar(&c, pdMS_TO_TICKS(10))) continue;
        if (c == '\r' || c == '\n') { video_write("\r\n"); return line; }
        if (c == '\b' || c == 0x7F) { if (line.length()) { line.remove(line.length() - 1); if (echo) video_write("\b \b"); } }
        else if (c >= 0x20 && c <= 0x7E) { line += c; if (echo) video_putc(c); }
    }
}

/* ---- HID report -> ASCII / VT100 ------------------------------------------------
 * Held keys auto-repeat after 400 ms at 30 Hz (the first draft never repeated). */
static uint8_t  s_prevKeys[6];
static uint8_t  s_heldKey, s_heldMod;
static uint32_t s_heldSince, s_lastRepeat;

static void emitKey(uint8_t k, uint8_t mod)
{
    bool shift = mod & 0x22, ctrl = mod & 0x11;
    if (k == 0x52) { enqueueKeySeq("\x1B[A"); return; }
    if (k == 0x51) { enqueueKeySeq("\x1B[B"); return; }
    if (k == 0x4F) { enqueueKeySeq("\x1B[C"); return; }
    if (k == 0x50) { enqueueKeySeq("\x1B[D"); return; }
    if (k == 0x4A) { enqueueKeySeq("\x1B[H"); return; }
    if (k == 0x4D) { enqueueKeySeq("\x1B[F"); return; }
    if (k == 0x4C) { enqueueKeySeq("\x1B[3~"); return; }
    if (k == 0x4B) { enqueueKeySeq("\x1B[5~"); return; }
    if (k == 0x4E) { enqueueKeySeq("\x1B[6~"); return; }
    if (k == 0x28) { enqueueKeyChar('\r'); return; }
    if (k == 0x29) { enqueueKeyChar('\x1B'); return; }
    if (k == 0x2A) { enqueueKeyChar(0x7F); return; }
    if (k == 0x2B) { enqueueKeyChar('\t'); return; }
    if (k == 0x2C) { enqueueKeyChar(ctrl ? 0 : ' '); return; }
    if (k >= 0x04 && k <= 0x1D) { enqueueKeyChar(ctrl ? (char)(k - 0x03) : (char)((shift ? 'A' : 'a') + k - 0x04)); return; }
    static const char num[] = "1234567890", numS[] = "!@#$%^&*()";
    if (k >= 0x1E && k <= 0x27) { enqueueKeyChar(shift ? numS[k - 0x1E] : num[k - 0x1E]); return; }
    static const char pun[] = "-=[]\\#;'`,./", punS[] = "_+{}|~:\"~<>?";
    if (k >= 0x2D && k <= 0x38) {
        char c = shift ? punS[k - 0x2D] : pun[k - 0x2D];
        if (ctrl && c == '[') c = 0x1B; else if (ctrl && c == '\\') c = 0x1C; else if (ctrl && c == ']') c = 0x1D;
        enqueueKeyChar(c);
    }
}

static void translateHidReport(const uint8_t *rpt)
{
    uint8_t mod = rpt[0];
    s_heldKey = 0;
    for (int i = 2; i < 8; i++) {
        uint8_t k = rpt[i];
        if (!k) continue;
        s_heldKey = k; s_heldMod = mod;
        bool held = false;
        for (int j = 0; j < 6; j++) if (s_prevKeys[j] == k) held = true;
        if (!held) { emitKey(k, mod); s_heldSince = millis(); s_lastRepeat = s_heldSince; }
    }
    memcpy(s_prevKeys, rpt + 2, 6);
}

static void serviceKeyRepeat()
{
    if (!s_heldKey) return;
    uint32_t now = millis();
    if (now - s_heldSince > 400 && now - s_lastRepeat > 33) { s_lastRepeat = now; emitKey(s_heldKey, s_heldMod); }
}

/* ---- BLE HID host (NimBLE-Arduino 2.x) ----------------------------------------- */
static NimBLEAddress s_bleAddr;
static volatile bool s_bleNeedConnect;
static const NimBLEUUID HID_SVC_UUID((uint16_t)0x1812);

static void bleNotifyCB(NimBLERemoteCharacteristic *, uint8_t *p, size_t len, bool)
{
    if (len == 9) translateHidReport(p + 1); else if (len >= 8) translateHidReport(p);
}
static void startScan() { NimBLEDevice::getScan()->start(0, false, true); }

class BleScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override
    {
        if (dev->isAdvertisingService(HID_SVC_UUID)) {
            s_bleAddr = dev->getAddress();
            s_bleStatus = "Found " + String(dev->getName().c_str());
            NimBLEDevice::getScan()->stop();
            s_bleNeedConnect = true;
        }
    }
    void onScanEnd(const NimBLEScanResults &, int) override { if (!s_bleNeedConnect) startScan(); }
} s_scanCB;

class BleClientCB : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient *, int reason) override { s_bleStatus = "Disconnected, rescanning"; memset(s_prevKeys, 0, 6); s_heldKey = 0; startScan(); }
} s_clientCB;

static void bleConnect()
{
    NimBLEClient *c = NimBLEDevice::getClientByPeerAddress(s_bleAddr);
    if (!c) { c = NimBLEDevice::createClient(); if (!c) return; c->setClientCallbacks(&s_clientCB, false); }
    if (!c->connect(s_bleAddr) || !c->secureConnection()) { s_bleStatus = "Connect failed, rescanning"; startScan(); return; }
    NimBLERemoteService *svc = c->getService(HID_SVC_UUID);
    int n = 0;
    if (svc) for (auto *ch : svc->getCharacteristics(true)) if (ch->canNotify() && ch->subscribe(true, bleNotifyCB)) n++;
    if (!n) { c->disconnect(); s_bleStatus = "No HID reports, rescanning"; startScan(); return; }
    s_bleStatus = "Connected (" + String(s_bleAddr.toString().c_str()) + ")";
}

/* ---- SSH-2 session (LibSSH-ESP32) --------------------------------------------- */
static void runSshCommand(const String &userHost, unsigned port)
{
    int at = userHost.indexOf('@');
    if (at <= 0) { video_write("Usage: ssh <user>@<host> [port]\r\n"); return; }
    String user = userHost.substring(0, at), host = userHost.substring(at + 1);
    if (WiFi.status() != WL_CONNECTED) { video_write("Wi-Fi not connected: wifi join <ssid> <pass>\r\n"); return; }
    video_write(("Connecting to " + host + ":" + String(port) + " as " + user + "...\r\n").c_str());

    ssh_session s = ssh_new();
    if (!s) { video_write("ssh_new failed\r\n"); return; }
    ssh_options_set(s, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(s, SSH_OPTIONS_USER, user.c_str());
    ssh_options_set(s, SSH_OPTIONS_PORT, &port);                 /* unsigned int, as libssh expects */
    if (ssh_connect(s) != SSH_OK) { video_write(("SSH connect: " + String(ssh_get_error(s)) + "\r\n").c_str()); ssh_free(s); return; }
    /* Host key: this first version does not verify the server key (no known_hosts
     * store yet), so it is open to an active interception on the local network. */
    video_write((user + "@" + host + "'s password: ").c_str());
    String pw = readLine(false);
    if (ssh_userauth_password(s, nullptr, pw.c_str()) != SSH_AUTH_SUCCESS) { video_write("Authentication failed.\r\n"); ssh_disconnect(s); ssh_free(s); return; }

    ssh_channel ch = ssh_channel_new(s);
    if (!ch || ssh_channel_open_session(ch) != SSH_OK || ssh_channel_request_pty_size(ch, "vt100", 80, 25) != SSH_OK || ssh_channel_request_shell(ch) != SSH_OK) {
        video_write("Could not open a shell.\r\n");
        if (ch) ssh_channel_free(ch);
        ssh_disconnect(s); ssh_free(s); return;
    }
    char buf[256];
    while (ssh_channel_is_open(ch) && !ssh_channel_is_eof(ch)) {
        int n = ssh_channel_read_nonblocking(ch, buf, sizeof buf, 0);
        if (n > 0) video_write_buf((const uint8_t *)buf, n); else if (n < 0) break;
        serviceKeyRepeat();
        char k;
        while (pollKeyChar(&k, 0)) ssh_channel_write(ch, &k, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    video_write("\r\n[SSH session closed]\r\n");
    ssh_channel_send_eof(ch); ssh_channel_close(ch); ssh_channel_free(ch);
    ssh_disconnect(s); ssh_free(s);
}

static void runTelnetCommand(const String &host, uint16_t port)
{
    WiFiClient c;
    video_write(("Connecting to " + host + ":" + String(port) + " (Ctrl+] to exit)...\r\n").c_str());
    if (!c.connect(host.c_str(), port)) { video_write("Connection failed.\r\n"); return; }
    while (c.connected()) {
        while (c.available()) video_putc((char)c.read());
        serviceKeyRepeat();
        char k;
        while (pollKeyChar(&k, 0)) { if (k == 0x1D) { c.stop(); video_write("\r\n[Telnet disconnected]\r\n"); return; } c.write((uint8_t)k); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    video_write("\r\n[Remote host closed]\r\n");
}

/* ---- shell --------------------------------------------------------------------- */
static Preferences s_prefs;

static void executeShellCommand(String line)
{
    line.trim();
    if (!line.length()) return;
    int sp = line.indexOf(' ');
    String cmd = sp == -1 ? line : line.substring(0, sp), args = sp == -1 ? "" : line.substring(sp + 1);
    args.trim();
    if (cmd == "help") {
        video_write("Commands:\r\n  wifi scan | wifi join <ssid> <pass> | wifi status\r\n  ssh <user>@<host> [port]\r\n  telnet <host> [port]\r\n  ls | cat <f> | rm <f> | ed <f>\r\n  ble | free | clear | reboot\r\n");
    } else if (cmd == "clear") video_write("\x1B[2J\x1B[H");
    else if (cmd == "free") {
        char b[160];
        snprintf(b, sizeof b, "Heap free %u (largest block %u)\r\nLittleFS %u / %u bytes\r\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
        video_write(b);
    } else if (cmd == "ble") video_write(("BLE: " + s_bleStatus + "\r\n").c_str());
    else if (cmd == "wifi") {
        if (args == "scan") { int n = WiFi.scanNetworks(); for (int i = 0; i < n; i++) video_write(("  " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)\r\n").c_str()); }
        else if (args.startsWith("join ")) {
            String rest = args.substring(5); int p = rest.indexOf(' ');
            String ssid = p == -1 ? rest : rest.substring(0, p), pass = p == -1 ? "" : rest.substring(p + 1);
            WiFi.begin(ssid.c_str(), pass.c_str());
            for (int t = 0; t < 20 && WiFi.status() != WL_CONNECTED; t++) { delay(500); video_putc('.'); }
            if (WiFi.status() == WL_CONNECTED) { s_prefs.putString("ssid", ssid); s_prefs.putString("pass", pass); video_write(("\r\nConnected, IP " + WiFi.localIP().toString() + "\r\n").c_str()); }
            else video_write("\r\nConnection timed out.\r\n");
        } else video_write(WiFi.status() == WL_CONNECTED ? ("Connected to " + WiFi.SSID() + ", IP " + WiFi.localIP().toString() + "\r\n").c_str() : "Wi-Fi disconnected.\r\n");
    } else if (cmd == "ssh") {
        int p = args.indexOf(' ');
        runSshCommand(p == -1 ? args : args.substring(0, p), p == -1 ? 22u : (unsigned)args.substring(p + 1).toInt());
    } else if (cmd == "telnet") {
        int p = args.indexOf(' ');
        runTelnetCommand(p == -1 ? args : args.substring(0, p), p == -1 ? 23 : (uint16_t)args.substring(p + 1).toInt());
    } else if (cmd == "ls") {
        File root = LittleFS.open("/");
        for (File f = root.openNextFile(); f; f = root.openNextFile()) video_write(("  " + String(f.name()) + "\t(" + String(f.size()) + " B)\r\n").c_str());
    } else if (cmd == "cat") {
        File f = LittleFS.open("/" + args, "r");
        if (!f) video_write("File not found.\r\n"); else { while (f.available()) video_putc((char)f.read()); video_write("\r\n"); f.close(); }
    } else if (cmd == "rm") { if (!LittleFS.remove("/" + args)) video_write("File not found.\r\n"); }
    else if (cmd == "ed") {
        if (!args.length()) { video_write("Usage: ed <file>\r\n"); return; }
        File f = LittleFS.open("/" + args, "a");
        if (!f) { video_write("Cannot open file.\r\n"); return; }
        video_write("Enter lines, '.' alone saves:\r\n");
        for (;;) { String l = readLine(true); if (l == ".") break; f.println(l); }
        f.close(); video_write("Saved.\r\n");
    } else if (cmd == "reboot") ESP.restart();
    else video_write(("Unknown command '" + cmd + "'. Type help.\r\n").c_str());
}

void setup()
{
    Serial.begin(115200);
    s_keyQueue = xQueueCreate(128, sizeof(char));
    video_init();
    LittleFS.begin(true);
    libssh_begin();

    NimBLEDevice::init("AirTag-VT100");
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

    video_write("\x1B[2J\x1B[H AIRTAG MICRO-OS (FreeRTOS + VT100 + SSH-2)\r\n Type help.\r\n\r\n");
}

void loop()
{
    if (s_bleNeedConnect) { s_bleNeedConnect = false; bleConnect(); }
    serviceKeyRepeat();
    video_write("airtag:~$ ");
    executeShellCommand(readLine(true));
}
