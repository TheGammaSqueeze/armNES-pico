# Third party notices

The armNES-pico blob (`armnes-pico.blob`) is built from the following. As a whole it is under the
GNU General Public License, version 2 (`LICENSE`). The MIT, zlib and newlib parts listed below are
under permissive licences that only ask for their notices to be kept, and libgcc under the GCC
Runtime Library Exception.

## armNES (GPL-2.0)

- Source: <https://github.com/MagnominusGit/armNES>, the `upstream/` submodule, at tag v1.1
  (480a9bc), the commit recorded in this repository (and in `BUILD_INFO.txt` of a release).
- Copyright (c) 2026 Magnominus. armNES as a whole is distributed under the GNU General Public
  License, version 2 (`upstream/LICENSE`, the same text as `LICENSE` here); its own files are
  GPL-2.0-or-later.
- Compiled: the core (`src/`), FCEUmm's boards and their bridge (`src/fceu/`), the libretro port
  (`libretro/libretro.c`, with `palettes.inc`, `vs-palettes.inc` and `font.inc`), its NTSC filter
  (`libretro/composite.c`) and the options helper (`shared/libretro/options.c`). Not compiled: the
  archive readers in `libretro/deps/` (only three of their headers are read, for the declarations
  `pico/pico_libc.c` answers), the standalone app (`app/`, the `armulators` submodule) and the test
  tools (`tests/`).
- Parts of it under other licences, as upstream states:
  - **TriCNES** (Copyright (c) 2025 Chris Siebert, MIT, `upstream/LICENSES/MIT.txt`): the PPU model
    in `src/ppu.c` and the frame counter, DMC, `$4015` and controller timing in `src/apu.c` follow
    it.
  - **emu2413** (Copyright (c) 2001-2019 Mitsutaka Okazaki, MIT,
    `upstream/LICENSES/MIT-emu2413.txt`): the VRC7's sound, `src/sound/emu2413.c`.
  - **FCEUmm** (Xodnizel, CaH4e3, NewRisingSun, the libretro team and others, GPL-2.0-or-later):
    the boards in `src/fceu/boards/`, the mapping functions in `src/fceu/bridge.c` and the
    palettes in `libretro/palettes.inc`.
  - **libretro.h** (`shared/libretro/libretro.h`, Copyright (C) 2010-2024 The RetroArch team, MIT):
    the libretro interface header the port and the glue are compiled with. The libretro-common
    headers read from `libretro/deps/libretro-common/include` (`file/archive_file.h`,
    `file/file_path.h`, `lists/string_list.h` and what they include, Copyright (C) 2010-2020 The
    RetroArch team) carry the same MIT notice; no code from them is in the blob.
- Both MIT texts ship in a release as `licenses/armNES-MIT.txt` and `licenses/armNES-MIT-emu2413.txt`.

## zlib (zlib licence)

- Source: <https://github.com/madler/zlib>, the `zlib/` submodule, at tag v1.3.1 (51b7f2a).
- Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler (`crc32.c`: Copyright (C) 1995-2022
  Mark Adler). Only `crc32.c` is compiled (with `zlib.h`,
  `zconf.h`, `zutil.h` and `crc32.h`), unmodified: armNES uses `crc32()` to identify games. The
  licence is in `zlib/LICENSE` and ships in a release as `licenses/zlib-LICENSE.txt`.

## This repository (GPL-2.0-or-later)

The glue in `pico/`, the harness and scripts in `tools/`, `build.sh` and the CI workflow: Copyright
(c) 2026 TheGammaSqueeze, GPL-2.0-or-later. `pico/nes_core_abi.h` is a copy of the interface header
of the GammaOS LK (lk-mt6785) by the same author, provided here under the same terms.

## The C runtime linked into the blob

- **newlib** (Red Hat and the newlib contributors): the string, stdio (`fopen`, `fread`, `fseek`,
  `ftell`, `fclose`, `fgets`, `snprintf`), locale and ctype functions the port calls, and from libm
  `sinf`, `cosf`, `floor`, `lrintf`, `fminf`, `fmaxf` and their helpers (the link map,
  `out/armnes-pico.map`, lists every object). newlib is a collection of files under BSD style and
  similar permissive licences; the libm files carry the fdlibm notice ("Permission to use, copy,
  modify, and distribute this software is freely granted, provided that this notice is
  preserved"). The notices are in newlib's `COPYING.NEWLIB` and, for the toolchain the releases are
  built with (Ubuntu's `libnewlib-arm-none-eabi`), in the copyright file shipped in a release as
  `licenses/newlib-COPYRIGHT.txt`.
- **libgcc** (the GCC project): 64 bit division and a few double precision helpers. GPL-3.0 with
  the GCC Runtime Library Exception, which places no conditions on the compiled blob.

## Corresponding source

The GPL asks that whoever receives the blob can obtain its complete source. Every release carries
`armnes-pico-<version>-src.tar.gz`, this repository with the `upstream` and `zlib` submodules at
the exact commits the blob was built from. `./build.sh` rebuilds the blob from it.
