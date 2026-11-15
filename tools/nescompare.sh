#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 TheGammaSqueeze
# Builds tools/nescompare.c natively twice, against upstream as it is and against out/src with the
# patches and their defines (build.sh's DEFS), runs both on a game and compares what they print:
# every frame's picture and sound, and the state CRCs.
#
#   tools/nescompare.sh <game> [frames]      (needs cc and zlib's headers)
#
# The CMP_* variables of tools/nescompare.c (BIOS, buttons, disk changes, state checkpoints) go to
# both runs. Save states also hold the core's scheduling fields (how far ahead the CPU may run
# unchecked, whether every cycle is stepped in full), which a patch for speed may change: a state
# CRC that differs is worth a look with CMP_STATE_DIR, a picture or sound that differs is a bug.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
[ $# -ge 1 ] || { echo "usage: tools/nescompare.sh <game> [frames]" >&2; exit 2; }
GAME=$1
FRAMES=${2:-3600}
OUT=$ROOT/out/compare
DEFS=$(sed -n 's/^DEFS="\(.*\)"$/\1/p' build.sh)
./build.sh src >/dev/null
mkdir -p "$OUT"

build() {
	local src=$1 defs=$2 exe=$3
	(cd "$src" && cc -O2 -w $defs -Isrc -Isrc/boards -Isrc/fceu -Isrc/fceu/boards -Ishared/libretro \
		src/*.c src/boards/*.c src/sound/emu2413.c src/fceu/bridge.c src/fceu/boards/*.c \
		"$ROOT/tools/nescompare.c" -lz -lm -o "$exe")
}
build "$ROOT/upstream" "" "$OUT/nescompare-upstream"
build "$ROOT/out/src" "$DEFS" "$OUT/nescompare-patched"

"$OUT/nescompare-upstream" "$GAME" "$FRAMES" > "$OUT/upstream.txt"
"$OUT/nescompare-patched" "$GAME" "$FRAMES" > "$OUT/patched.txt"
if diff <(grep -v ' state ' "$OUT/upstream.txt") <(grep -v ' state ' "$OUT/patched.txt") >/dev/null; then
	echo "pictures and sound identical over $FRAMES frames ($(tail -n 1 "$OUT/patched.txt"))"
else
	echo "pictures or sound differ, first at:"
	diff <(grep -v ' state ' "$OUT/upstream.txt") <(grep -v ' state ' "$OUT/patched.txt") | head -n 4
	exit 1
fi
if diff <(grep ' state ' "$OUT/upstream.txt") <(grep ' state ' "$OUT/patched.txt") >/dev/null; then
	echo "state CRCs identical"
else
	echo "state CRCs differ (see the note above)"
fi
