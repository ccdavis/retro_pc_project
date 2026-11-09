#!/usr/bin/env bash
# Installs the local RP2350 and CP/M host toolchains under tools/ (no sudo):
#   - tools/pico-sdk           Raspberry Pi Pico SDK 2.1.1 (+ tinyusb submodule)
#   - tools/arm-gnu-toolchain  Arm GNU Toolchain 13.3.Rel1 (arm-none-eabi-gcc)
#   - tools/qemu               qemu-system-arm (for Cortex-M33 verification)
#   - tools/RunCPM             RunCPM 6.9 with our BDOS 220..224 host extensions
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
mkdir -p "$HERE/dl"

echo "=== [1/4] Pico SDK 2.1.1 ==="
if [ ! -d "$HERE/pico-sdk/.git" ]; then
    git clone --branch 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git "$HERE/pico-sdk"
    git -C "$HERE/pico-sdk" submodule update --init lib/tinyusb
else
    echo "  already present: $HERE/pico-sdk"
fi

echo "=== [2/4] Arm GNU Toolchain 13.3.Rel1 (x86_64 -> arm-none-eabi) ==="
if [ ! -x "$HERE/arm-gnu-toolchain/bin/arm-none-eabi-gcc" ]; then
    TAR="$HERE/dl/arm-gnu-toolchain.tar.xz"
    URL="https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz"
    [ -f "$TAR" ] || curl -fL "$URL" -o "$TAR"
    mkdir -p "$HERE/arm-gnu-toolchain"
    tar -xJf "$TAR" --strip-components=1 -C "$HERE/arm-gnu-toolchain"
else
    echo "  already present: $HERE/arm-gnu-toolchain"
fi

echo "=== [3/4] RunCPM (CP/M 2.2 emulator + BDOS 220..224 patch) ==="
PATCH_DIR="$ROOT/02_beetle_terminal_os/os_cpm22_runcpm/runcpm_patch"
if [ ! -d "$HERE/RunCPM/.git" ]; then
    git clone https://github.com/MockbaTheBorg/RunCPM.git "$HERE/RunCPM"
    git -C "$HERE/RunCPM" checkout -q 12fc57a1784b8fc97dae30eb5dffedccf098bbd5
fi
cp "$PATCH_DIR/runcpm_host_bdos.h" "$HERE/RunCPM/RunCPM/"
if git -C "$HERE/RunCPM" apply --check "$PATCH_DIR/runcpm_cpm_h.patch" 2>/dev/null; then
    git -C "$HERE/RunCPM" apply "$PATCH_DIR/runcpm_cpm_h.patch"
fi
if [ ! -x "$HERE/RunCPM/RunCPM/RunCPM" ]; then
    make -s -C "$HERE/RunCPM/RunCPM" -f Makefile.posix CCP=INTERNAL
fi
echo "  ready: $HERE/RunCPM/RunCPM/RunCPM"

echo "=== [4/4] QEMU (qemu-system-arm) ==="
if [ -x "$HERE/qemu/usr/bin/qemu-system-arm" ]; then
    echo "  already present: $HERE/qemu/usr/bin/qemu-system-arm"
elif command -v qemu-system-arm >/dev/null 2>&1; then
    mkdir -p "$HERE/qemu/usr/bin" "$HERE/qemu/usr/share"
    ln -sf "$(command -v qemu-system-arm)" "$HERE/qemu/usr/bin/qemu-system-arm"
    [ -d /usr/share/qemu ] && ln -sfn /usr/share/qemu "$HERE/qemu/usr/share/qemu"
    echo "  linked system qemu-system-arm into $HERE/qemu"
elif command -v apt-get >/dev/null 2>&1 && command -v dpkg-deb >/dev/null 2>&1; then
    (cd "$HERE/dl" && apt-get download qemu-system-arm qemu-system-data)
    mkdir -p "$HERE/qemu"
    for deb in "$HERE/dl"/qemu-system-*.deb; do
        dpkg-deb -x "$deb" "$HERE/qemu"
    done
    echo "  extracted qemu-system-arm into $HERE/qemu"
else
    echo "  [WARN] install qemu-system-arm via your system package manager for --arm tests"
fi
