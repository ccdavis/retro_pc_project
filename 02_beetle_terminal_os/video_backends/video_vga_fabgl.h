/**
 * Video Backend 2: 5-Resistor Direct 640x480 VGA via FabGL
 * Target Hardware: Classic Dual-Core ESP32 Beetle (DFR0575 / ESP32-Pico)
 * Required Library: FabGL (install via Arduino Library Manager)
 *
 * Wiring (to VGA receptacle or VGA-to-HDMI adapter behind monitor):
 *   Beetle GPIO 22 ---[ 270 Ohm ]---> VGA Pin 1  (Red)
 *   Beetle GPIO 21 ---[ 270 Ohm ]---> VGA Pin 2  (Green)
 *   Beetle GPIO 19 ---[ 270 Ohm ]---> VGA Pin 3  (Blue)
 *   Beetle GPIO 18 ---[  47 Ohm ]---> VGA Pin 13 (HSync)
 *   Beetle GPIO 5  ---[  47 Ohm ]---> VGA Pin 14 (VSync)
 *   Beetle GND     -----------------> VGA Pins 5, 6, 7, 8, 10 (GND)
 */

#ifndef VIDEO_VGA_FABGL_H
#define VIDEO_VGA_FABGL_H

#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <fabgl.h>

static fabgl::VGAController s_vgaDisplay;
static fabgl::Terminal      s_vgaTerminal;

static inline void video_init() {
    // Initialize 8-color 640x480 @ 60Hz VGA using 5 pins
    s_vgaDisplay.begin(GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_19, GPIO_NUM_18, GPIO_NUM_5);
    s_vgaDisplay.setResolution(VGA_640x480_60Hz);

    s_vgaTerminal.begin(&s_vgaDisplay);
    s_vgaTerminal.setBackgroundColor(Color::Black);
    s_vgaTerminal.setForegroundColor(Color::BrightGreen);
    s_vgaTerminal.clear();
    s_vgaTerminal.enableCursor(true);
}

static inline void video_putc(char c) {
    Serial.write(c);
    s_vgaTerminal.write((uint8_t)c);
}

static inline void video_write(const char* str) {
    while (*str) {
        video_putc(*str++);
    }
}

static inline void video_write_buf(const uint8_t* buf, size_t len) {
    Serial.write(buf, len);
    s_vgaTerminal.write(buf, len);
}

#else
#error "FabGL VGA backend requires a classic dual-core ESP32 target. Use VIDEO_BACKEND_COMPOSITE_SPI or VIDEO_BACKEND_UART_RP2350_HDMI on ESP32-C3/C6."
#endif

#endif // VIDEO_VGA_FABGL_H
