#!/usr/bin/env bash
# Compile one Arduino sketch for a Beetle variant with the arduino-cli installed by
# install_arduino_cli.sh.   usage: compile_sketch.sh <variant> <sketch.ino> [extra -D flags]
#   variants: dfrobot_beetle_esp32c3 | dfrobot_beetle_esp32c6
# The sketch's directory is copied to a temp folder named after the sketch (Arduino
# requires the folder and file names to match) together with ../video_backends and
# the sibling headers it includes.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
A="$HERE/arduino"
export ARDUINO_DIRECTORIES_DATA="$A/data" ARDUINO_DIRECTORIES_DOWNLOADS="$A/downloads" ARDUINO_DIRECTORIES_USER="$A/user"
variant="$1"; case "$variant" in *:*) ;; *) variant="$variant:PartitionScheme=huge_app" ;; esac   # 3 MB app + 1 MB LittleFS, no OTA: the sketches are 1.3-1.6 MB
sketch="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; shift 2
name="$(basename "$sketch" .ino)"
T="$(mktemp -d)/$name"; mkdir -p "$T"
cp "$(dirname "$sketch")"/* "$T"/ 2>/dev/null || true
[ -d "$(dirname "$sketch")/../video_backends" ] && cp -r "$(dirname "$sketch")/../video_backends" "$T/../video_backends"
"$A/arduino-cli" compile --fqbn "esp32:esp32:$variant" --build-property "compiler.cpp.extra_flags=$*" --warnings default "$T"
