#!/usr/bin/env bash
# Fetch the upstream DECtalk source tree (github.com/dectalk/dectalk @ 69ebb459,
# git-ignored, Fonix proprietary licence), apply patches/upstream.diff, and
# generate build-gen/maindict.c (the full 15,473-entry US dictionary in the
# ARM7 CHEESY_DICT_COMPRESSION layout).
set -euo pipefail

D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
U="$D/dectalk-upstream/dectalk"
UP="$U/src/dapi/src"
COMMIT="69ebb459137a7a8d92ed41da8362233eaa173efc"
CC="${HOST_CC:-gcc}"

if [ ! -d "$U/.git" ]; then
    mkdir -p "$D/dectalk-upstream"
    git clone https://github.com/dectalk/dectalk.git "$U"
fi

if [ "$(git -C "$U" rev-parse HEAD 2>/dev/null || true)" != "$COMMIT" ]; then
    git -C "$U" fetch -q origin "$COMMIT" 2>/dev/null || true
    git -C "$U" checkout -q "$COMMIT"
fi

if git -C "$U" apply --check "$D/patches/upstream.diff" 2>/dev/null; then
    git -C "$U" apply "$D/patches/upstream.diff"
    echo "DECtalk upstream patches applied"
else
    echo "DECtalk upstream patches already applied"
fi

if [ ! -f "$D/build-gen/maindict.c" ]; then
    echo "Building DECtalk US dictionary (build-gen/maindict.c)..."
    mkdir -p "$D/build-gen"
    "$CC" -O2 -w -DANSI -DENGLISH_US -DACCESS32 -DTYPING_MODE -DLTSSIM -DTTSSIM \
        -I"$UP/include" -I"$UP/api" -I"$UP/cmd" -I"$UP/dic" -I"$UP/kernel" \
        -I"$UP/lts" -I"$UP/ph" -I"$UP/nt" -I"$UP/osf" -I"$UP/protos" \
        -I"$UP/vtm" -I"$UP/../.." \
        -o "$D/build-gen/dic_us" "$UP/dic/dic.c"
    (cd "$D/build-gen" && ./dic_us "$UP/dic/Dic_us.txt" dtalk_us.dic >/dev/null)
    python3 "$D/glue/dic2c.py" "$D/build-gen/dtalk_us.dic" "$D/build-gen/maindict.c"
fi

echo "DECtalk ready in $D"
