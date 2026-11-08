#!/usr/bin/env bash
# ==============================================================================
# AirTag Game Consoles & 68000 Systems Fetch/Build Helper
# Supports:
#   - atari5200 : alijani1/Pico2Atari8              (Atari 5200 / 800XL on RP2350)
#   - sms       : PicoPlus-devel/pico-smsplus       (Sega Master System / Game Gear)
#   - nes       : PicoPlus-devel/pico-infonesPlus   (NES on RP2350-Zero HW_CONFIG=6)
#   - genesis   : PicoPlus-devel/pico-genesisPlus   (Sega Genesis / Mega Drive)
#   - snes      : PicoPlus-devel/pico-snesPlus      (Super Nintendo on RP2350 + PSRAM)
#   - mac       : evansm7/pico-mac                  (Macintosh 128K/512K umac in SRAM)
#   - amiga     : MiSTle-Dev/NanoMig                (Amiga 500/1200 FPGA Core for Tang 25K SoM)
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
    atari5200|atari800)
        echo "=== Fetching Atari 5200 / 800XL Emulator (alijani1/Pico2Atari8) ==="
        clone_or_update "https://github.com/alijani1/Pico2Atari8.git" "Pico2Atari8"
        ;;
    sms|coleco)
        echo "=== Fetching Sega Master System / SG-1000 / Coleco Emulator (pico-smsplus) ==="
        clone_or_update "https://github.com/PicoPlus-devel/pico-smsplus.git" "pico-smsplus"
        ;;
    nes)
        echo "=== Fetching NES Emulator (PicoPlus-devel/pico-infonesPlus - HW_CONFIG=6 is RP2350-Zero) ==="
        clone_or_update "https://github.com/PicoPlus-devel/pico-infonesPlus.git" "pico-infonesPlus"
        ;;
    genesis)
        echo "=== Fetching Sega Genesis / Mega Drive Emulator (PicoPlus-devel/pico-genesisPlus) ==="
        clone_or_update "https://github.com/PicoPlus-devel/pico-genesisPlus.git" "pico-genesisPlus"
        ;;
    snes)
        echo "=== Fetching Super Nintendo Emulator (PicoPlus-devel/pico-snesPlus) ==="
        clone_or_update "https://github.com/PicoPlus-devel/pico-snesPlus.git" "pico-snesPlus"
        ;;
    mac)
        echo "=== Fetching Apple Macintosh 128K/512K Emulator (evansm7/pico-mac / umac) ==="
        clone_or_update "https://github.com/evansm7/pico-mac.git" "pico-mac"
        ;;
    amiga)
        echo "=== Fetching NanoMig Amiga 500/1200 FPGA Core (MiSTle-Dev/NanoMig for Tang 25K SoM) ==="
        clone_or_update "https://github.com/MiSTle-Dev/NanoMig.git" "NanoMig"
        ;;
    *)
        echo "Usage: $0 {atari5200|coleco|sms|nes|genesis|snes|mac|amiga}"
        exit 0
        ;;
esac
