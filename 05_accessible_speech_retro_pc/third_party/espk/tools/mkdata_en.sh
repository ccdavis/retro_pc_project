#!/bin/sh
# Build the English-only data pack: phoneme tables base1 + en only (phondata
# 186 KB instead of 600 KB), en_dict, intonations; packed by mkdat.py.
# Needs the host build in build-host/ (cmake, see NOTES.md).  usage: mkdata_en.sh out.dat
set -e
A="$(cd "$(dirname "$0")/.." && pwd)"; OUT="$1"; E="$A/build-host/src/espeak-ng"
T="$(mktemp -d)"; mkdir -p "$T/phsource" "$T/data" "$T/dict"
cp -r "$A/upstream/phsource/"* "$T/phsource/"
# master phoneme file: everything up to the end of table base1, then table en
{ sed -n '1,/^phonemetable consonants/p' "$A/upstream/phsource/phonemes" | sed '$d'; echo; echo "phonemetable en base1"; echo "include ph_english"; } > "$T/phsource/phonemes"
cp "$A/build-host/espeak-ng-data/intonations" "$T/data/"
cp -r "$A/build-host/espeak-ng-data/lang" "$A/build-host/espeak-ng-data/voices" "$T/data/"
cp "$A/upstream/dictsource/en_list" "$A/upstream/dictsource/en_rules" "$A/upstream/dictsource/en_emoji" "$T/dict/"
(cd "$T/phsource" && ESPEAK_DATA_PATH="$T/data" "$E" --path="$T/data" --compile-phonemes | tail -1)
(cd "$T/dict" && ESPEAK_DATA_PATH="$T/data" "$E" --path="$T/data" --compile=en | tail -1)
uv run --no-project "$A/tools/mkdat.py" "$T/data" "$OUT"
rm -rf "$T"
