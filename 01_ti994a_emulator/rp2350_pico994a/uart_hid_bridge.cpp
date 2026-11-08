/* uart_hid_bridge.cpp -- see uart_hid_bridge.h.  Compiled as part of pico-994A. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "gamepad.h"            /* pico_shared: io::KeyboardState, io::GamePadState */
#include "uart_hid_bridge.h"

#define RX_RING 256
static uint8_t  s_ring[RX_RING];
static volatile uint32_t s_w, s_r;
static uint8_t  s_frame[13], s_idx;
static uint8_t  s_joy;
static uint32_t s_ok, s_bad;

static void __not_in_flash_func(uart_irq)(void)
{
    while (uart_is_readable(BEETLE_UART_ID)) {
        uint8_t c = (uint8_t)uart_get_hw(BEETLE_UART_ID)->dr;
        uint32_t next = (s_w + 1) & (RX_RING - 1);
        if (next == s_r) continue;                /* overrun: drop, the next frame resyncs */
        s_ring[s_w] = c;
        __dmb();
        s_w = next;
    }
}

extern "C" void beetle_uart_hid_init(void)
{
    uart_init(BEETLE_UART_ID, BEETLE_UART_BAUD);
    gpio_set_function(BEETLE_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(BEETLE_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(BEETLE_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(BEETLE_UART_ID, true);
    irq_set_exclusive_handler(BEETLE_UART_IRQ, uart_irq);
    irq_set_enabled(BEETLE_UART_IRQ, true);
    uart_set_irq_enables(BEETLE_UART_ID, true, false);
}

static void apply_frame(const uint8_t *f)
{
    /* keyboard: the structure the emulator scans each frame */
    auto &kb = const_cast<io::KeyboardState &>(io::getCurrentKeyboardState());
    kb.connected = true;
    kb.modifier = f[3];
    memcpy(kb.keycode, &f[5], 6);

    /* joystick byte -> gamepad 0 */
    s_joy = f[11];
    auto &gp = io::getCurrentGamePadState(0);
    gp.connected = true;
    gp.GamePadName = "Beetle";
    gp.GamePadShortName = "BT";
    int b = 0;
    if (s_joy & 0x01) b |= io::GamePadState::Button::RIGHT;
    if (s_joy & 0x02) b |= io::GamePadState::Button::LEFT;
    if (s_joy & 0x04) b |= io::GamePadState::Button::DOWN;
    if (s_joy & 0x08) b |= io::GamePadState::Button::UP;
    if (s_joy & 0x10) b |= io::GamePadState::Button::START;
    if (s_joy & 0x20) b |= io::GamePadState::Button::SELECT;
    if (s_joy & 0x40) b |= io::GamePadState::Button::B;
    if (s_joy & 0x80) b |= io::GamePadState::Button::A;
    gp.buttons = b;
}

extern "C" void beetle_uart_hid_poll(void)
{
    while (s_r != s_w) {
        uint8_t c = s_ring[s_r];
        s_r = (s_r + 1) & (RX_RING - 1);
        if (s_idx == 0) { if (c == 0xAA) s_frame[s_idx++] = c; }
        else if (s_idx == 1) { if (c == 0x55) s_frame[s_idx++] = c; else s_idx = (c == 0xAA) ? 1 : 0; }
        else {
            s_frame[s_idx++] = c;
            if (s_idx == 13) {
                uint8_t x = 0;
                for (int i = 2; i <= 11; i++) x ^= s_frame[i];
                if (x == s_frame[12] && s_frame[2] == 0x01) { apply_frame(s_frame); s_ok++; } else s_bad++;
                s_idx = 0;
            }
        }
    }
}

extern "C" uint8_t  beetle_get_joy_mask(void) { return s_joy; }
extern "C" uint32_t beetle_frames_received(void) { return s_ok; }
extern "C" uint32_t beetle_frames_bad(void) { return s_bad; }
