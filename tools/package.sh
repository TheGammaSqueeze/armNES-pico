#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 TheGammaSqueeze
# Package a built blob for release (run ./build.sh first):
#
#   tools/package.sh            -> dist/armnes-pico-<version>.zip
#                                  dist/armnes-pico-<version>-src.tar.gz
#
# The zip is what goes on the SD card plus the licence texts: cores/armnes-pico.blob, LICENSE (the
# GPL, version 2), THIRD_PARTY_NOTICES.md, README.md and licenses/ (armNES's two MIT texts, zlib's
# licence and, when the toolchain ships it, newlib's copyright file, since the blob links parts of
# newlib). The source archive is the complete source the GPL asks to go with the blob: this
# repository and the upstream and zlib submodules at the commits it was built from.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f out/armnes-pico.blob ] || { echo "out/armnes-pico.blob missing: run ./build.sh first" >&2; exit 1; }

VER=${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo unknown)}
NAME=armnes-pico-$VER
PACK=dist/$NAME
rm -rf "$PACK" "dist/$NAME.zip" "dist/$NAME-src.tar.gz"
mkdir -p "$PACK/cores" "$PACK/licenses"

cp out/armnes-pico.blob "$PACK/cores/"
cp LICENSE THIRD_PARTY_NOTICES.md README.md "$PACK/"
cp upstream/LICENSES/MIT.txt "$PACK/licenses/armNES-MIT.txt"
cp upstream/LICENSES/MIT-emu2413.txt "$PACK/licenses/armNES-MIT-emu2413.txt"
cp zlib/LICENSE "$PACK/licenses/zlib-LICENSE.txt"
NEWLIB_DOC=$(ls /usr/share/doc/libnewlib-arm-none-eabi/copyright 2>/dev/null || true)
if [ -n "$NEWLIB_DOC" ]; then
	cp "$NEWLIB_DOC" "$PACK/licenses/newlib-COPYRIGHT.txt"
else
	echo "note: newlib's copyright file not found; see THIRD_PARTY_NOTICES.md" >&2
fi
(cd "$PACK/cores" && sha256sum armnes-pico.blob > armnes-pico.blob.sha256)
{
	echo "armNES-pico $VER"
	echo "upstream armNES $(git -C upstream describe --tags --always 2>/dev/null || git -C upstream rev-parse --short HEAD) ($(git -C upstream rev-parse HEAD))"
	echo "zlib $(git -C zlib describe --tags --always 2>/dev/null || git -C zlib rev-parse --short HEAD) ($(git -C zlib rev-parse HEAD))"
	echo "toolchain $(${CC:-arm-none-eabi-gcc} --version | head -1)"
	echo "blob sha256 $(cut -d' ' -f1 "$PACK/cores/armnes-pico.blob.sha256")"
} > "$PACK/BUILD_INFO.txt"
(cd dist && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME")

# The complete source: every tracked file, the submodules' included (armNES's own armulators
# submodule is only needed for its standalone app and is not part of it)
git ls-files --recurse-submodules -z | tar --null -czf "dist/$NAME-src.tar.gz" --transform "s,^,$NAME-src/," -T -

echo "dist/$NAME.zip"
echo "dist/$NAME-src.tar.gz"
