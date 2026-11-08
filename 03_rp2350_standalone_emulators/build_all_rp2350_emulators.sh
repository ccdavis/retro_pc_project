#!/usr/bin/env bash
# ==============================================================================
# Standalone Waveshare RP2350-Zero Multi-System Emulator Builder
# Clones & configures open-source RP2350 emulators for the AirTag RP2350-Zero:
#   1) apple2e : rh1tech/frank-apple              (Apple //e + Mockingboard)
#   2) apple2c : ajtaji/AppleIIcEmulatorPicoSeries (Apple //c 128K PureMetal SRAM)
#   3) pcxt    : xrip/pico-286                    (IBM PC XT / PCjr / 286 + TGA/CGA + SN76489/AdLib)
#   4) c64     : rh1tech/frank-c64                (Commodore 64 VIC-II + SID)
#   5) ti994a  : PicoPlus-devel/pico-994A         (TI-99/4A TMS9900 + TMS9918A + Speech)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET="${1:-help}"

clone_or_update() {
    local url="$1"
    local dir="$2"
    if [ ! -d "${SCRIPT_DIR}/${dir}" ]; then
        git clone --recursive "${url}" "${SCRIPT_DIR}/${dir}"
    else
        echo "Directory ${dir} already exists."
    fi
}

case "${TARGET}" in
    apple2e)
        echo "=== Fetching Apple //e Emulator (rh1tech/frank-apple) ==="
        clone_or_update "https://github.com/rh1tech/frank-apple.git" "frank-apple"
        ;;
    apple2c)
        echo "=== Fetching Apple //c 128K SRAM Emulator (ajtaji/AppleIIcEmulatorPicoSeries) ==="
        clone_or_update "https://github.com/ajtaji/AppleIIcEmulatorPicoSeries.git" "apple2c-pico2"
        ;;
    pcxt|pcjr)
        echo "=== Fetching IBM PC XT / PCjr / 286 Emulator (xrip/pico-286) ==="
        clone_or_update "https://github.com/xrip/pico-286.git" "pico-286"
        ;;
    c64)
        echo "=== Fetching Commodore 64 Emulator (rh1tech/frank-c64) ==="
        clone_or_update "https://github.com/rh1tech/frank-c64.git" "frank-c64"
        ;;
    ti994a)
        echo "=== Fetching TI-99/4A Emulator (PicoPlus-devel/pico-994A) ==="
        clone_or_update "https://github.com/PicoPlus-devel/pico-994A.git" "pico-994A"
        ;;
    *)
        echo "Usage: $0 {apple2e|apple2c|pcjr|pcxt|c64|ti994a}"
        exit 0
        ;;
esac
