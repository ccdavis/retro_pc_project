#!/usr/bin/env bash
# Build pico-994A (TI-99/4A) for the Waveshare RP2350-Zero with the Beetle UART keyboard bridge.
#   HW_CONFIG 2 ("Adafruit DVI + microSD breakouts / custom PCB") is the PicoPlus configuration
#   with HSTX HDMI on GPIO 12-19 (D0+ 12, CK+ 14, D2+ 16, D1+ 18, D- = D+ + 1) and the SD card
#   on SPI0 GPIO 2/3/4/5.  Its NES pad pins (6-11) and LED pin are disabled here because
#   GPIO 6/7 carry the Beetle's PS/2 lines.  HW_CONFIG 6 ("RP2350-Zero with custom PCB") is a
#   PIO-DVI pinout on GPIO 6-13 and has no HSTX branch; use it only for that wiring.
#   The Beetle link is UART1 on GPIO 8 (TX) / 9 (RX): UART0 stays pico-994A's console.
#   Release build: pico-994A's Debug build currently fails to link (SAMS_SwapBank undefined),
#   an upstream issue.
#   Verified 2026-11-07: pico994A.uf2 built (447 KB text, 251 KB bss) with Pico SDK 2.1.1.
#
#   PICO_SDK_PATH=/home/ccd/retro_pc_project/tools/pico-sdk \
#   PICO_TOOLCHAIN_PATH=/home/ccd/retro_pc_project/tools/arm-gnu-toolchain/bin ./build_pico994a.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${SCRIPT_DIR}/pico-994A"

echo "=== [1/4] pico-994A sources (with submodules: pico_shared, pico_lib, tusb_xinput; the Pico SDK needs its lib/tinyusb submodule) ==="
if [ ! -d "${REPO_DIR}" ]; then
    git clone --recursive https://github.com/PicoPlus-devel/pico-994A.git "${REPO_DIR}"
fi

echo "=== [2/4] Beetle UART keyboard bridge ==="
cp "${SCRIPT_DIR}/uart_hid_bridge.h" "${SCRIPT_DIR}/uart_hid_bridge.cpp" "${REPO_DIR}/"
if git -C "${REPO_DIR}" apply --check "${SCRIPT_DIR}/pico994a_uart_hid.patch" 2>/dev/null; then
    git -C "${REPO_DIR}" apply "${SCRIPT_DIR}/pico994a_uart_hid.patch"
    echo "  patch applied (main.cpp: init + poll, CMakeLists.txt: source)"
elif grep -q beetle_uart_hid_poll "${REPO_DIR}/main.cpp"; then
    echo "  patch already applied"
else
    echo "  !! pico994a_uart_hid.patch does not apply to this pico-994A revision; add the two calls by hand (see uart_hid_bridge.h)"; exit 1
fi

echo "=== [3/4] toolchain ==="
: "${PICO_SDK_PATH:?set PICO_SDK_PATH to a Pico SDK 2.x checkout}"
export PICO_SDK_PATH
EXTRA=()
[ -n "${PICO_TOOLCHAIN_PATH:-}" ] && EXTRA+=("-DPICO_TOOLCHAIN_PATH=${PICO_TOOLCHAIN_PATH}")

echo "=== [4/4] build: HW_CONFIG=2 (HSTX pins 12-19, SD 2-5), waveshare_rp2350_zero, Release ==="
cmake -S "${REPO_DIR}" -B "${REPO_DIR}/build" -G Ninja \
      -DPICO_BOARD=waveshare_rp2350_zero -DPICO_PLATFORM=rp2350-arm-s -DCMAKE_BUILD_TYPE=Release \
      -DHW_CONFIG=2 -DUSE_HSTX=1 \
      -DNES_CLK=-1 -DNES_DATA=-1 -DNES_LAT=-1 -DNES_CLK_1=-1 -DNES_DATA_1=-1 -DNES_LAT_1=-1 -DLED_GPIO_PIN=-1 \
      "${EXTRA[@]}"
ninja -C "${REPO_DIR}/build"
ls -l "${REPO_DIR}"/build/*.uf2
echo "Hold BOOT on the RP2350-Zero while plugging in USB and copy the .uf2 to the RPI-RP2 drive."
