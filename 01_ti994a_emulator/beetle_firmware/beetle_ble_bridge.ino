/**
 * beetle_ble_bridge.ino -- Board 1: Beetle ESP32-C3 / ESP32-C6 firmware
 * BLE keyboard host -> UART HID frames to the RP2350-Zero, PS/2 device output,
 * Wi-Fi web pad for the TI-99/4A function keys.
 *
 * Boards: DFRobot Beetle ESP32-C6 (DFR1117, 512 KB SRAM, 4 MB flash) or
 *         Beetle ESP32-C3 (DFR0868, 400 KB SRAM, 4 MB flash).  Both 25 x 20.5 mm.
 *         (The classic Beetle ESP32 DFR0575 is 35 x 34 mm and does not fit the case.)
 * Core:   arduino-esp32 3.x.  Libraries: NimBLE-Arduino 2.x (this file uses the 2.x
 *         API: NimBLEScanCallbacks, const NimBLEAdvertisedDevice*, onDisconnect(.., reason)).
 *
 * Pins (from the board variant's pins_arduino.h, so they are right on both boards):
 *   UART1 TX  = TX pin  (C6: GPIO16, C3: GPIO21) ----> RP2350-Zero GPIO 1 (UART0 RX)
 *   UART1 RX  = RX pin  (C6: GPIO17, C3: GPIO20) <---- RP2350-Zero GPIO 0 (UART0 TX)
 *   PS/2 CLK  = GPIO 4  ----> RP2350-Zero GPIO 6  (for PS/2 emulators, see PLAN.md)
 *   PS/2 DATA = GPIO 5  ----> RP2350-Zero GPIO 7
 *   5V <-> 5V, GND <-> GND
 *
 * Frame to the RP2350 (13 bytes): AA 55 01 mod res k0 k1 k2 k3 k4 k5 joy xor(bytes 2..11)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <NimBLEDevice.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C6)
#warning "Written for the Beetle ESP32-C3/C6; check the pin table for other boards"
#endif

#ifndef RP2350_UART_TX_PIN
#define RP2350_UART_TX_PIN  TX      /* variant default UART pins */
#endif
#ifndef RP2350_UART_RX_PIN
#define RP2350_UART_RX_PIN  RX
#endif
#define PS2_CLK_PIN         4
#define PS2_DATA_PIN        5
#define RP2350_BAUD_RATE    115200

const char *AP_SSID = "AirTag-TI99";
const char *AP_PASS = "ti994a1981";

WebServer server(80);
HardwareSerial RP2350Serial(1);

/* ---- HID state ------------------------------------------------------------- */
struct __attribute__((packed)) HidKeyboardReport { uint8_t modifier, reserved, keycode[6]; };
static HidKeyboardReport currentReport = {}, prevReport = {};
static uint8_t currentJoyMask = 0;

#define HID_MOD_LCTRL  0x01
#define HID_MOD_LSHIFT 0x02
#define HID_MOD_LALT   0x04   /* TI FCTN in pico-994A */
#define HID_MOD_LGUI   0x08
#define HID_MOD_RCTRL  0x10
#define HID_MOD_RSHIFT 0x20
#define HID_MOD_RALT   0x40

void sendHidStateToRP2350(const HidKeyboardReport &kbd, uint8_t joyMask)
{
    uint8_t f[13] = { 0xAA, 0x55, 0x01, kbd.modifier, kbd.reserved,
                      kbd.keycode[0], kbd.keycode[1], kbd.keycode[2], kbd.keycode[3], kbd.keycode[4], kbd.keycode[5],
                      joyMask, 0 };
    for (int i = 2; i <= 11; i++) f[12] ^= f[i];
    RP2350Serial.write(f, sizeof f);
}

/* ---- PS/2 device output (bit-banged, open-drain) ----------------------------
 * Timing 20/40/20 us gives a 12.5 kHz clock, inside the 10-16.7 kHz PS/2 range.
 * Wi-Fi and BLE tasks can preempt loop(), so a frame can be stretched; PS/2
 * hosts tolerate that.  The host may inhibit by holding CLK low: we wait. */
static void ps2SetIdle() { pinMode(PS2_CLK_PIN, INPUT_PULLUP); pinMode(PS2_DATA_PIN, INPUT_PULLUP); }

static void ps2WriteBit(uint8_t v)
{
    if (v) pinMode(PS2_DATA_PIN, INPUT_PULLUP); else { pinMode(PS2_DATA_PIN, OUTPUT); digitalWrite(PS2_DATA_PIN, LOW); }
    delayMicroseconds(20);
    pinMode(PS2_CLK_PIN, OUTPUT); digitalWrite(PS2_CLK_PIN, LOW);
    delayMicroseconds(40);
    pinMode(PS2_CLK_PIN, INPUT_PULLUP);
    delayMicroseconds(20);
}

static void ps2WriteByte(uint8_t data)
{
    uint32_t t0 = micros();
    while (digitalRead(PS2_CLK_PIN) == LOW && micros() - t0 < 20000) {}   /* host inhibiting */
    uint8_t parity = 1;
    ps2WriteBit(0);
    for (int i = 0; i < 8; i++) { uint8_t b = (data >> i) & 1; ps2WriteBit(b); parity ^= b; }
    ps2WriteBit(parity);
    ps2WriteBit(1);
    ps2SetIdle();
    delayMicroseconds(100);
}

/* USB HID usage -> PS/2 Set 2 make code (0 = not mapped) */
static uint8_t hidToPs2Set2(uint8_t k, bool *ext)
{
    static const uint8_t letters[26] = { 0x1C,0x32,0x21,0x23,0x24,0x2B,0x34,0x33,0x43,0x3B,0x42,0x4B,0x3A,
                                         0x31,0x44,0x4D,0x15,0x2D,0x1B,0x2C,0x3C,0x2A,0x1D,0x22,0x35,0x1A };
    static const uint8_t digits[10]  = { 0x16,0x1E,0x26,0x25,0x2E,0x36,0x3D,0x3E,0x46,0x45 };
    static const uint8_t fkeys[12]   = { 0x05,0x06,0x04,0x0C,0x03,0x0B,0x83,0x0A,0x01,0x09,0x78,0x07 };
    *ext = false;
    if (k >= 0x04 && k <= 0x1D) return letters[k - 0x04];
    if (k >= 0x1E && k <= 0x27) return digits[k - 0x1E];
    if (k >= 0x3A && k <= 0x45) return fkeys[k - 0x3A];
    switch (k) {
        case 0x28: return 0x5A;  /* Enter */        case 0x29: return 0x76;  /* Esc */
        case 0x2A: return 0x66;  /* Backspace */    case 0x2B: return 0x0D;  /* Tab */
        case 0x2C: return 0x29;  /* Space */        case 0x2D: return 0x4E;  /* - */
        case 0x2E: return 0x55;  /* = */            case 0x2F: return 0x54;  /* [ */
        case 0x30: return 0x5B;  /* ] */            case 0x31: return 0x5D;  /* backslash */
        case 0x33: return 0x4C;  /* ; */            case 0x34: return 0x52;  /* ' */
        case 0x35: return 0x0E;  /* ` */            case 0x36: return 0x41;  /* , */
        case 0x37: return 0x49;  /* . */            case 0x38: return 0x4A;  /* / */
        case 0x39: return 0x58;  /* Caps Lock */
        case 0x4F: *ext = true; return 0x74;  /* Right */  case 0x50: *ext = true; return 0x6B;  /* Left */
        case 0x51: *ext = true; return 0x72;  /* Down */   case 0x52: *ext = true; return 0x75;  /* Up */
        case 0x49: *ext = true; return 0x70;  /* Insert */ case 0x4C: *ext = true; return 0x71;  /* Delete */
        case 0x4A: *ext = true; return 0x6C;  /* Home */   case 0x4D: *ext = true; return 0x69;  /* End */
        case 0x4B: *ext = true; return 0x7D;  /* PgUp */   case 0x4E: *ext = true; return 0x7A;  /* PgDn */
        default: return 0;
    }
}

static void ps2SendKeyEvent(uint8_t hidKey, bool pressed)
{
    bool ext = false;
    uint8_t sc = hidToPs2Set2(hidKey, &ext);
    if (!sc) return;
    if (ext) ps2WriteByte(0xE0);
    if (!pressed) ps2WriteByte(0xF0);
    ps2WriteByte(sc);
}

static void ps2SendModifier(uint8_t bit, uint8_t next, uint8_t prev, uint8_t sc, bool ext)
{
    if (!((next ^ prev) & bit)) return;
    if (ext) ps2WriteByte(0xE0);
    if (!(next & bit)) ps2WriteByte(0xF0);
    ps2WriteByte(sc);
}

static bool keyInReport(uint8_t key, const HidKeyboardReport &r)
{
    if (!key) return false;
    for (int i = 0; i < 6; i++) if (r.keycode[i] == key) return true;
    return false;
}

static void syncPs2FromHidReport(const HidKeyboardReport &next, const HidKeyboardReport &prev)
{
    ps2SendModifier(HID_MOD_LSHIFT, next.modifier, prev.modifier, 0x12, false);
    ps2SendModifier(HID_MOD_LCTRL,  next.modifier, prev.modifier, 0x14, false);
    ps2SendModifier(HID_MOD_LALT,   next.modifier, prev.modifier, 0x11, false);
    ps2SendModifier(HID_MOD_RSHIFT, next.modifier, prev.modifier, 0x59, false);
    ps2SendModifier(HID_MOD_RCTRL,  next.modifier, prev.modifier, 0x14, true);
    ps2SendModifier(HID_MOD_RALT,   next.modifier, prev.modifier, 0x11, true);
    for (int i = 0; i < 6; i++) { uint8_t k = prev.keycode[i]; if (k && !keyInReport(k, next)) ps2SendKeyEvent(k, false); }
    for (int i = 0; i < 6; i++) { uint8_t k = next.keycode[i]; if (k && !keyInReport(k, prev)) ps2SendKeyEvent(k, true); }
}

static void updateHidState(const HidKeyboardReport &r)
{
    currentReport = r;
    sendHidStateToRP2350(currentReport, currentJoyMask);
    syncPs2FromHidReport(currentReport, prevReport);
    prevReport = currentReport;
}

static void tapKeyCombo(uint8_t mod, uint8_t hidKey, uint16_t holdMs = 60)
{
    HidKeyboardReport press = { mod, 0, { hidKey, 0, 0, 0, 0, 0 } };
    updateHidState(press);
    delay(holdMs);
    HidKeyboardReport release = {};
    updateHidState(release);
}

/* ---- BLE HID host (NimBLE-Arduino 2.x) --------------------------------------- */
static NimBLEAddress targetAddr;          /* copied out of the scan result: the result object is not kept */
static volatile bool doBleConnect = false, bleConnected = false;
static String connectedBleName = "Scanning...";
static const NimBLEUUID HID_SERVICE_UUID((uint16_t)0x1812);
static const NimBLEUUID HID_REPORT_CHAR_UUID((uint16_t)0x2A4D);
static const NimBLEUUID HID_BOOT_KBD_IN_UUID((uint16_t)0x2A22);

static void hidNotifyCallback(NimBLERemoteCharacteristic *, uint8_t *pData, size_t length, bool)
{
    HidKeyboardReport rpt;
    if (length == 9) memcpy(&rpt, pData + 1, 8);          /* report ID prefix */
    else if (length >= 8) memcpy(&rpt, pData, 8);
    else return;
    updateHidState(rpt);
}

static void startScan() { NimBLEDevice::getScan()->start(0, false, true); }

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *) override { bleConnected = true; }
    void onDisconnect(NimBLEClient *, int reason) override
    {
        bleConnected = false;
        connectedBleName = "Disconnected (" + String(reason) + "), rescanning";
        startScan();
    }
} clientCB;

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override
    {
        if (dev->isAdvertisingService(HID_SERVICE_UUID)) {
            targetAddr = dev->getAddress();
            connectedBleName = dev->getName().length() ? String(dev->getName().c_str()) : String(targetAddr.toString().c_str());
            NimBLEDevice::getScan()->stop();
            doBleConnect = true;
        }
    }
    void onScanEnd(const NimBLEScanResults &, int) override { if (!doBleConnect && !bleConnected) startScan(); }
} scanCB;

static bool connectToBleKeyboard()
{
    NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(targetAddr);
    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        if (!pClient) return false;
        pClient->setClientCallbacks(&clientCB, false);
        pClient->setConnectionParams(12, 12, 0, 150);
    }
    if (!pClient->connect(targetAddr)) return false;
    if (!pClient->secureConnection()) { pClient->disconnect(); return false; }   /* keyboards require bonding */

    NimBLERemoteService *svc = pClient->getService(HID_SERVICE_UUID);
    if (!svc) { pClient->disconnect(); return false; }
    int subscribed = 0;
    for (NimBLERemoteCharacteristic *ch : svc->getCharacteristics(true)) {
        if ((ch->getUUID() == HID_BOOT_KBD_IN_UUID || ch->getUUID() == HID_REPORT_CHAR_UUID) && ch->canNotify()) {
            if (ch->subscribe(true, hidNotifyCallback)) subscribed++;
        }
    }
    if (!subscribed) { pClient->disconnect(); return false; }
    return true;
}

/* ---- Wi-Fi web pad --------------------------------------------------------------
 * k(mod, hid): mod 4 = Left Alt = TI FCTN; hid = USB HID usage in decimal.
 * Digits: '1'=30 '2'=31 '3'=32 '4'=33 '5'=34 '6'=35 '7'=36 '8'=37 '9'=38 '0'=39
 * (the first draft used Backspace/Esc/Enter codes for FCTN+9/8/7). */
static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AirTag TI-99/4A Controller</title>
<style>body{background:#181a1b;color:#f0f0f0;font-family:monospace;text-align:center;margin:12px}
h2{color:#ff6b00;margin-bottom:4px}.status{background:#26292b;padding:8px;border-radius:6px;margin-bottom:12px;font-size:13px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:420px;margin:0 auto 14px}
button{background:#33383b;color:#fff;border:1px solid #555;border-radius:6px;padding:12px 6px;font-family:monospace;font-size:13px;font-weight:bold}
button:active{background:#ff6b00;color:#000}.accent{background:#8a3800;border-color:#ff6b00}.menu-btn{background:#1b4d7e;border-color:#4da3ff}</style></head>
<body><h2>TI-99/4A AIRTAG CONTROLLER</h2><div class="status">BLE Keyboard: <b>%BLE_STATUS%</b></div>
<div class="grid">
<button class="menu-btn" onclick="k(0,58)">ROM MENU (F1)</button>
<button class="accent" onclick="k(4,46)">QUIT (FCTN+=)</button>
<button onclick="k(4,38)">BACK (FCTN+9)</button>
<button onclick="k(4,37)">REDO (FCTN+8)</button>
<button onclick="k(4,36)">AID (FCTN+7)</button>
<button onclick="k(4,35)">PROC'D (FCTN+6)</button>
<button onclick="k(4,33)">CLEAR (FCTN+4)</button>
<button onclick="k(4,32)">ERASE (FCTN+3)</button>
<button onclick="k(4,30)">DEL (FCTN+1)</button>
<button onclick="k(0,30)">1 (TI BASIC)</button>
<button onclick="k(0,31)">2 (CARTRIDGE)</button>
<button onclick="k(0,40)">ENTER</button>
</div><h3>JOYSTICK 1 / ARROWS</h3><div class="grid">
<button onclick="k(0,41)">ESC</button><button onclick="k(0,82)">UP (E)</button><button class="accent" onclick="k(0,44)">FIRE (SPACE)</button>
<button onclick="k(0,80)">LEFT (S)</button><button onclick="k(0,81)">DOWN (X)</button><button onclick="k(0,79)">RIGHT (D)</button>
</div><script>function k(m,c){fetch('/key?m='+m+'&k='+c);}</script></body></html>
)rawliteral";

static void setupWebServer()
{
    server.on("/", HTTP_GET, []() {
        String html = FPSTR(WEB_UI_HTML);
        html.replace("%BLE_STATUS%", connectedBleName);
        server.send(200, "text/html", html);
    });
    server.on("/key", HTTP_GET, []() {
        uint8_t mod  = server.hasArg("m") ? (uint8_t)server.arg("m").toInt() : 0;
        uint8_t code = server.hasArg("k") ? (uint8_t)server.arg("k").toInt() : 0;
        if (code) tapKeyCombo(mod, code, 60);
        server.send(200, "text/plain", "OK");
    });
    server.begin();
}

void setup()
{
    ps2SetIdle();
    RP2350Serial.begin(RP2350_BAUD_RATE, SERIAL_8N1, RP2350_UART_RX_PIN, RP2350_UART_TX_PIN);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    MDNS.begin("airtag-ti99");
    setupWebServer();

    NimBLEDevice::init("AirTag-TI99-Host");
    NimBLEDevice::setSecurityAuth(true, false, true);    /* bonding, no MITM, secure connections */
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCB, false);
    scan->setInterval(45);
    scan->setWindow(15);
    scan->setActiveScan(true);
    startScan();
}

void loop()
{
    server.handleClient();
    if (doBleConnect) {
        doBleConnect = false;
        if (!connectToBleKeyboard()) { connectedBleName = "Connect failed, rescanning"; startScan(); }
    }
    delay(2);
}
