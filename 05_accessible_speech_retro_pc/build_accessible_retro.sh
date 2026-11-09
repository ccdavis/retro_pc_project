#!/usr/bin/env bash
# =============================================================================
# build_accessible_retro.sh -- build and verify the Core 1 speech coprocessor
#
#   ./build_accessible_retro.sh            host tests with the real engines + SPEAK.COM
#   ./build_accessible_retro.sh --arm      + the same code on QEMU's Cortex-M33 (sizes, instruction counts)
#   ./build_accessible_retro.sh --pico     + Pico SDK firmware for the RP2350-Zero (UF2)
#   ./build_accessible_retro.sh --cpm      + run SPEAK.COM under CP/M 2.2 (RunCPM / z80pack)
#   ./build_accessible_retro.sh --whisper  + transcribe the host WAVs with Whisper (uv, downloads a model once)
#   ./build_accessible_retro.sh --all
#
# Engine sources live under third_party/{espk,klatt,dectalk}.
# =============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

DO_ARM=0; DO_PICO=0; DO_CPM=0; DO_WHISPER=0
for a in "$@"; do
    case "$a" in
        --arm) DO_ARM=1 ;; --pico) DO_PICO=1 ;; --cpm) DO_CPM=1 ;; --whisper) DO_WHISPER=1 ;;
        --all) DO_ARM=1; DO_PICO=1; DO_CPM=1; DO_WHISPER=1 ;;
        *) echo "unknown option $a"; exit 2 ;;
    esac
done

ROOT="$(cd "$HERE/.." && pwd)"
DT_DIR=${DT_DIR:-"$HERE/third_party/dectalk"}
ESPK_DIR=${ESPK_DIR:-"$HERE/third_party/espk"}
KLATT=${KLATT:-"$HERE/third_party/klatt"}
TOOLS=${TOOLS:-"$ROOT/tools"}
export DT_DIR ESPK_DIR KLATT TOOLS

echo "=== [1] sources and tools ==="
ok() { echo "  [OK]   $1"; }
warn() { echo "  [WARN] $1"; }
if [ ! -f "$DT_DIR/build-gen/maindict.c" ] && [ -x "$DT_DIR/get_dectalk.sh" ]; then
    "$DT_DIR/get_dectalk.sh" || true
fi
[ -f "$DT_DIR/build-gen/maindict.c" ] && ok "DECtalk ARM7 tree + full dictionary: $DT_DIR" || warn "DECtalk not found at $DT_DIR (run third_party/dectalk/get_dectalk.sh; host tests will use ESPK/tone only)"
[ -f "$ESPK_DIR/espk/espk.c" ] && [ -f "$KLATT/klatt_fx.c" ] && ok "ESPK + klatt_fx: $ESPK_DIR" || warn "ESPK not found at $ESPK_DIR"
command -v z80asm >/dev/null && ok "z80asm $(z80asm 2>&1 | grep -o 'version [0-9.]*' | head -1)" || warn "z80asm missing: keeping the prebuilt cpm_disk_A/SPEAK.COM"
[ -x "$TOOLS/arm-gnu-toolchain/bin/arm-none-eabi-gcc" ] && ok "arm-none-eabi-gcc in $TOOLS" || warn "no ARM toolchain in $TOOLS (run ../tools/install_rp2350_tools.sh for --arm/--pico)"
[ -x "$TOOLS/qemu/usr/bin/qemu-system-arm" ] && ok "qemu-system-arm in $TOOLS" || warn "no qemu-system-arm in $TOOLS (run ../tools/install_rp2350_tools.sh for --arm)"
[ -d "$TOOLS/pico-sdk" ] && ok "pico-sdk in $TOOLS" || warn "no pico-sdk in $TOOLS (run ../tools/install_rp2350_tools.sh for --pico)"

echo "=== [2] SPEAK.COM (CP/M 2.2, BDOS 224) ==="
if command -v z80asm >/dev/null; then
    (cd cpm_disk_A && z80asm -fb -oSPEAK.COM speak.asm | tail -1 && ls -l SPEAK.COM)
fi

echo "=== [3] host tests with the real engines ==="
make -s -C host_test -j"$(nproc)"
mkdir -p host_test/out
./host_test/sim_core1 "$HERE/host_test/out"

if [ "$DO_CPM" = 1 ]; then
    echo "=== [4] SPEAK.COM under CP/M 2.2 (RunCPM) ==="
    if [ -x "$TOOLS/RunCPM/RunCPM/RunCPM" ]; then
        T=$(mktemp -d); mkdir -p "$T/A/0"
        cp cpm_disk_A/SPEAK.COM "$T/A/0/SPEAK.COM"
        OUT=$( (cd "$T" && printf 'SPEAK\rSPEAK /S7 /P60 HELLO WORLD\rEXIT\r' | timeout 10 "$TOOLS/RunCPM/RunCPM/RunCPM" 2>&1) | tr -d '\r' | cat -v )
        echo "$OUT" | grep -q "Usage: SPEAK" && ok "usage text printed" || { echo "$OUT" | tail -15; echo "  [FAIL] usage text"; exit 1; }
        echo "$OUT" | grep -q '\^A7S\^A60PHELLO WORLD' && ok "options -> ^A7S ^A60P, text follows (BDOS 224)" || { echo "$OUT" | tail -15; echo "  [FAIL] option bytes"; exit 1; }
        rm -rf "$T"
    else
        warn "RunCPM missing in $TOOLS/RunCPM (run ../tools/install_rp2350_tools.sh)"
    fi
fi

if [ "$DO_ARM" = 1 ]; then
    echo "=== [5] Cortex-M33 under QEMU (mps2-an505) ==="
    make -s -C arm_qemu -j"$(nproc)" TOOLS="$TOOLS"
    make -s -C arm_qemu run TOOLS="$TOOLS"
fi

if [ "$DO_PICO" = 1 ]; then
    echo "=== [6] Pico SDK firmware (waveshare_rp2350_zero) ==="
    cmake -S pico_firmware -B pico_firmware/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DDT_DIR="$DT_DIR" -DESPK_DIR="$ESPK_DIR" -DKLATT="$KLATT" \
          -DPICO_SDK_PATH="$TOOLS/pico-sdk" -DPICO_TOOLCHAIN_PATH="$TOOLS/arm-gnu-toolchain/bin" > pico_firmware/build/configure.log 2>&1 || { tail -20 pico_firmware/build/configure.log; exit 1; }
    ninja -C pico_firmware/build > pico_firmware/build/ninja.log 2>&1 || { grep -E "error|FAILED" pico_firmware/build/ninja.log | head; exit 1; }
    "$TOOLS/arm-gnu-toolchain/bin/arm-none-eabi-size" pico_firmware/build/talking_retro.elf
    ls -l pico_firmware/build/talking_retro.uf2
fi

if [ "$DO_WHISPER" = 1 ]; then
    echo "=== [7] Whisper base.en on the host WAVs ==="
    uv run host_test/tools/transcribe_np.py host_test/out/sentence_dectalk.wav host_test/out/sentence_espk.wav \
        host_test/out/boot_dectalk.wav host_test/out/boot_espk.wav host_test/out/flush_dectalk.wav
fi
echo "=== done ==="
