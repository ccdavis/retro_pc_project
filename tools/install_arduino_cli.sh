#!/usr/bin/env bash
# Installs arduino-cli, the ESP32 core and the two libraries the Beetle sketches
# need, under retro_pc_project/tools/arduino (no sudo).  The ESP32 core is a large
# download (about 1.5 GB of toolchains).  Afterwards compile a sketch with:
#
#   tools/compile_sketch.sh dfrobot_beetle_esp32c6 01_ti994a_emulator/beetle_firmware/beetle_ble_bridge.ino
#
# Installed 2026-11-07 (arduino-cli 1.5.2, esp32 3.3.12, NimBLE-Arduino 2.5.2,
# LibSSH-ESP32 5.10.0).  compile_sketch.sh adds PartitionScheme=huge_app: the
# sketches are 1.3-1.6 MB, more than the default 1.25 MB app partition.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
A="$HERE/arduino"
mkdir -p "$A" && cd "$A"
export ARDUINO_DIRECTORIES_DATA="$A/data" ARDUINO_DIRECTORIES_DOWNLOADS="$A/downloads" ARDUINO_DIRECTORIES_USER="$A/user"
if [ ! -x "$A/arduino-cli" ]; then
    curl -fsSL https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Linux_64bit.tar.gz -o arduino-cli.tar.gz
    tar xzf arduino-cli.tar.gz arduino-cli && rm arduino-cli.tar.gz
fi
./arduino-cli version
./arduino-cli config init --overwrite --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json >/dev/null
./arduino-cli core update-index
./arduino-cli core install esp32:esp32
./arduino-cli lib install "NimBLE-Arduino" "LibSSH-ESP32"
echo "Boards:"; ./arduino-cli board listall | grep -i -E "beetle|pico32" || true
