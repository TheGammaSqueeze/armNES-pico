#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 TheGammaSqueeze
# armNES-pico: build the armNES core blob for the GammaOS LK (lk-mt6785, Konkr Pocket Advance).
#
#   ./build.sh            -> out/armnes-pico.blob (copy it to the SD card as /cores/armnes-pico.blob)
#   ./build.sh src        -> only out/src: the upstream sources with patches/ applied
#   ./build.sh clean
#
# The upstream tree (submodule upstream/, MagnominusGit/armNES) is never modified: its src/, libretro/
# and shared/ are copied to out/src, the patches in patches/ (if any) are applied there, and that
# copy is compiled together with the glue in pico/. Built: armNES's core (src/, with FCEUmm's boards
# in src/fceu/), its libretro port (libretro/libretro.c and the NTSC filter, libretro/composite.c)
# and the options helper it shares with the Armulators cores (shared/libretro/options.c). Not built:
# the archive readers in libretro/deps (the LK unpacks a .zip itself and hands over the image), the
# standalone app and the test tools. armNES needs only crc32() from zlib; that one file comes from
# the zlib submodule (madler/zlib), unmodified.
#
# Target: the LK runs the core in 32 bit ARM state on CPU 0 of the MT6785, a Cortex-A55. Built for
# ARMv8-A with NEON and hardware floating point (softfp calling convention, as the LK's own NEON code
# and the armGBA blob), tuned for the A55, -O3 and link time optimisation.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
OUT=$ROOT/out
CC=${CC:-arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
# the source list below is sorted: sort the same everywhere, so the link order is too
export LC_ALL=C

if [ "${1:-}" = clean ]; then rm -rf "$OUT"; exit 0; fi

if [ ! -f upstream/src/nes.c ]; then
	git submodule update --init upstream
fi
if [ ! -f zlib/crc32.c ]; then
	git submodule update --init zlib
fi

UPSTREAM_REV=$(git -C upstream describe --tags --always 2>/dev/null || git -C upstream rev-parse --short HEAD)
PICO_REV=$(git describe --always --dirty 2>/dev/null || echo unknown)
VERSION="armNES ${UPSTREAM_REV} (pico ${PICO_REV})"

rm -rf "$OUT/src" "$OUT/obj"
mkdir -p "$OUT/src" "$OUT/obj"
cp -a upstream/src upstream/libretro upstream/shared "$OUT/src/"
# EXTRA_PATCHES: more patches for experiments, applied after these; releases use none
for p in patches/*.patch ${EXTRA_PATCHES:-}; do
	[ -e "$p" ] || continue
	echo ">> patch $(basename "$p")"
	case "$p" in /*) pp=$p;; *) pp=$ROOT/$p;; esac
	(cd "$OUT/src" && patch -p1 --quiet --forward < "$pp")
done
if [ "${1:-}" = src ]; then echo "out/src ready"; exit 0; fi

SRC=$OUT/src
# The LK runs on CPU 0, a Cortex-A55 (MIDR 0x411fd050), an in-order core: scheduling for it
# matters. TUNE, OPT and EXTRA_CFLAGS override for experiments.
TUNE=${TUNE:-cortex-a55}
ARCH="-march=armv8-a+simd -mtune=$TUNE -marm -mfpu=neon-fp-armv8 -mfloat-abi=softfp"
# The defines that turn the patches on (README.md, "Patches"); every file is built with them, as
# some change structures the glue shares
DEFS="-DARMNES_HOST_FDS_BATCH"
BASE="$ARCH -std=gnu11 ${OPT:--O3} -flto -ffunction-sections -fdata-sections -fno-common $DEFS ${EXTRA_CFLAGS:-}"
# shared/libretro comes before libretro-common's include folder, so "libretro.h" is armNES's copy
INC="-I$SRC/shared/libretro -I$SRC/src -I$SRC/src/boards -I$ROOT/zlib"
# armNES's core, as its CMakeLists.txt builds it (-O3, functions aligned to 64 bytes)
CORE_FLAGS="$BASE -falign-functions=64 -Wall -Wno-unused-parameter $INC -I$SRC/src/fceu -I$SRC/src/fceu/boards"
# FCEUmm's boards are built with warnings off upstream too
FCEU_FLAGS="$BASE -falign-functions=64 -w $INC -I$SRC/src/fceu -I$SRC/src/fceu/boards"
PORT_FLAGS="$BASE -Wall -Wno-unused-parameter $INC -I$SRC/libretro -I$SRC/libretro/deps/libretro-common/include"
GLUE_FLAGS="$BASE -Wall -Wextra -Wno-unused-parameter $INC -I$ROOT/pico"
# The C runtime is compiled outside LTO: newlib's archive members reference its symbols (_read,
# _calloc_r, ...) only at the final link, after LTO would already have dropped them. -fno-builtin as
# any malloc must be: GCC otherwise turns malloc + memset back into a call to calloc.
RT_FLAGS="${BASE/-flto/} -fno-builtin -Wall -Wextra -I$ROOT/pico"

# upstream CMakeLists.txt CORE_SOURCES, then src/fceu/bridge.c and every FCEUmm board
CORE="src/apu.c src/arcade.c src/boards/asic.c src/boards/bandai.c src/boards/basic.c
	src/boards/discrete.c src/boards/fds.c src/boards/jy.c src/boards/list.c src/boards/mmc3.c
	src/boards/mmc5.c src/boards/multicart.c src/boards/nsf.c src/boards/rare.c src/boards/sachen.c
	src/bus.c src/cart.c src/cheat.c src/cpu.c src/input.c src/nes.c src/ppu-fast.c src/ppu.c
	src/sound/emu2413.c src/state.c src/fceu/bridge.c"
FCEU=$(cd "$SRC" && ls src/fceu/boards/*.c | sort)
PORT="libretro/libretro.c libretro/composite.c shared/libretro/options.c"

OBJS=()
obj() { echo "$OUT/obj/$(echo "${1%.c}" | tr '/' '_').o"; }
for f in $CORE; do
	o=$(obj "$f")
	case "$f" in src/sound/emu2413.c) FL="${CORE_FLAGS/-Wall/-w}";; *) FL=$CORE_FLAGS;; esac
	$CC $FL -c "$SRC/$f" -o "$o"
	OBJS+=("$o")
done
for f in $FCEU; do
	o=$(obj "$f")
	$CC $FCEU_FLAGS -c "$SRC/$f" -o "$o"
	OBJS+=("$o")
done
for f in $PORT; do
	o=$(obj "$f")
	$CC $PORT_FLAGS -c "$SRC/$f" -o "$o"
	OBJS+=("$o")
done
$CC $BASE -w -c "$ROOT/zlib/crc32.c" -o "$OUT/obj/zlib_crc32.o"
OBJS+=("$OUT/obj/zlib_crc32.o")
$CC $GLUE_FLAGS -c "$ROOT/pico/pico_core.c" -o "$OUT/obj/pico_core.o"
$CC $RT_FLAGS -c "$ROOT/pico/pico_libc.c" -o "$OUT/obj/pico_libc.o"
OBJS+=("$OUT/obj/pico_core.o" "$OUT/obj/pico_libc.o")

$CC $BASE -nostartfiles -Wl,--gc-sections -Wl,-T,"$ROOT/pico/blob.ld" -Wl,-Map,"$OUT/armnes-pico.map" \
	-o "$OUT/armnes-pico.elf" "${OBJS[@]}" -lm -lc -lgcc
$OBJCOPY -O binary "$OUT/armnes-pico.elf" "$OUT/armnes-pico.blob"

python3 - "$OUT/armnes-pico.blob" "$VERSION" <<'EOF'
import struct, sys, hashlib
d = open(sys.argv[1], 'rb').read()
magic, ver, entry, load, span = struct.unpack_from('<5I', d)
assert magic == 0x3153454E and ver == 1, "bad header"
assert load == len(d), "load size %d != file %d" % (load, len(d))
assert entry < load, "entry outside the image"
slot = 0x50000000 - 0x4F800000
assert span <= slot, "span %d does not fit the 8 MiB core slot" % span
print("armnes-pico.blob: %s" % sys.argv[2])
print("  file %d bytes, span %d bytes (%.2f MiB of the 8 MiB slot), entry +0x%x" % (len(d), span, span / 1048576.0, entry))
print("  sha256 %s" % hashlib.sha256(d).hexdigest())
EOF
