/*
 * uart_hid_bridge.h -- Beetle -> pico-994A keyboard and joystick link over UART1.
 *
 * The Beetle sends 13-byte frames (AA 55 01 mod res k0..k5 joy xor) at 115200 8N1
 * into RP2350-Zero GPIO 9 (UART1 RX; GPIO 8 is UART1 TX, unused).  UART0 on
 * GPIO 0/1 stays with pico-994A's console and its own "serial keyboard" feature.
 *
 * pico-994A has no process_kbd_report(); its USB HID code fills the shared
 * io::KeyboardState (pico_shared/gamepad.h) and the emulator reads that every
 * frame.  This bridge writes the same structure from the UART frames, and maps
 * the joystick byte onto io::getCurrentGamePadState(0).
 *
 * Integration (done by build_pico994a.sh via pico994a_uart_hid.patch):
 *   main.cpp: #include "uart_hid_bridge.h"; beetle_uart_hid_init() after
 *             serialKeyboardIrqInit(); beetle_uart_hid_poll() after tuh_task().
 *   CMakeLists.txt: add uart_hid_bridge.cpp to the executable.
 */
#ifndef UART_HID_BRIDGE_H
#define UART_HID_BRIDGE_H
#include <stdint.h>
#include <stdbool.h>

#ifndef BEETLE_UART_ID
#define BEETLE_UART_ID      uart1
#define BEETLE_UART_IRQ     UART1_IRQ
#define BEETLE_UART_TX_PIN  8
#define BEETLE_UART_RX_PIN  9
#define BEETLE_UART_BAUD    115200
#endif

#ifdef __cplusplus
extern "C" {
#endif
void     beetle_uart_hid_init(void);
void     beetle_uart_hid_poll(void);        /* call from the main loop, once per frame */
uint8_t  beetle_get_joy_mask(void);         /* bit 0 right, 1 left, 2 down, 3 up, 4 start, 5 select, 6 B, 7 A */
uint32_t beetle_frames_received(void);
uint32_t beetle_frames_bad(void);
#ifdef __cplusplus
}
#endif
#endif
