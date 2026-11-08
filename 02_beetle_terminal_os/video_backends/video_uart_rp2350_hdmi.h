/**
 * Video Backend 3: UART-to-HDMI via Waveshare RP2350-Zero Display Card
 * Works on ANY Beetle ESP32 variant (0% CPU and 0 KB RAM overhead on the Beetle!)
 *
 * Wiring:
 *   Beetle 5V          <---> RP2350-Zero 5V
 *   Beetle GND         <---> RP2350-Zero GND
 *   Beetle TX (GPIO16) ----> RP2350-Zero GPIO 1 (UART0 RX @ 921600 baud)
 */

#ifndef VIDEO_UART_RP2350_HDMI_H
#define VIDEO_UART_RP2350_HDMI_H

#include <Arduino.h>

#ifndef HDMI_CARD_UART_TX_PIN
#define HDMI_CARD_UART_TX_PIN  16
#endif
#ifndef HDMI_CARD_UART_RX_PIN
#define HDMI_CARD_UART_RX_PIN  17
#endif
#define HDMI_CARD_BAUD_RATE    921600

static HardwareSerial s_hdmiSerial(1);

static inline void video_init() {
    s_hdmiSerial.begin(HDMI_CARD_BAUD_RATE, SERIAL_8N1, HDMI_CARD_UART_RX_PIN, HDMI_CARD_UART_TX_PIN);
    delay(50);
    // Send ANSI reset + clear screen to RP2350 HDMI VT100 Display Card
    s_hdmiSerial.print("\x1B[2J\x1B[H");
}

static inline void video_putc(char c) {
    Serial.write(c);
    s_hdmiSerial.write((uint8_t)c);
}

static inline void video_write(const char* str) {
    Serial.print(str);
    s_hdmiSerial.print(str);
}

static inline void video_write_buf(const uint8_t* buf, size_t len) {
    Serial.write(buf, len);
    s_hdmiSerial.write(buf, len);
}

#endif // VIDEO_UART_RP2350_HDMI_H
