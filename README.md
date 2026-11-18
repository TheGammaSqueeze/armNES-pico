# armNES-pico

[![Support armNES on Ko-fi](https://img.shields.io/badge/Ko--fi-support%20armNES-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/magnominus)

> **Enjoying armNES-pico? Please support the upstream project.** All of the emulation here is
> Magnominus's work on [armNES](https://github.com/MagnominusGit/armNES). If this core makes your
> games better, consider buying them a coffee on [Ko-fi](https://ko-fi.com/magnominus).

[armNES](https://github.com/MagnominusGit/armNES) by Magnominus, built as a loadable NES, Famicom and
Famicom Disk System core for the GammaOS bootloader emulator on the Konkr Pocket Advance (MediaTek
MT6785, `lk-mt6785`, branch `lk-gba-emu-sd-card-kpa`).

All of the emulation is armNES's. This repository adds no emulation code: it holds the upstream
tree as a submodule, the glue that lets armNES's libretro port run inside a bootloader with no
operating system, a build script and a test harness. No patch to upstream is applied (see
"Patches").

**This is an experimental project and does not accept issues.** Please do not report problems you
see with armNES-pico to armNES upstream either: this build runs armNES in a bootloader on hardware
upstream does not target, so its behaviour here says nothing about armNES itself.

## Credits

- **armNES** (Magnominus, GPL-2.0-or-later, [support them on Ko-fi](https://ko-fi.com/magnominus)):
  the NES and Famicom emulator, its three PPU models, the APU, the Disk System, its own boards, the
  libretro port this blob drives and its NTSC filter.
- armNES builds on the work of others, which this blob carries too (see upstream's README,
  "Credits"): **TriCNES** (Chris Siebert, MIT), whose PPU model and frame counter, DMC and
  controller timing armNES follows; **FCEUmm** (Xodnizel, CaH4e3, NewRisingSun, the libretro team
  and others, GPL-2.0-or-later), whose cartridge boards in `src/fceu/` and palettes armNES carries;
  **emu2413** (Mitsutaka Okazaki, MIT), the VRC7's sound.
- **zlib** (Jean-loup Gailly and Mark Adler): armNES takes the CRC-32 of each game from it.

## License

armNES is distributed under the GNU General Public License, version 2 (`LICENSE`, the same text as
upstream's), as FCEUmm's boards are; armNES's own files are GPL-2.0-or-later, and a few parts are
under the MIT licence (TriCNES's model in `src/ppu.c` and `src/apu.c`, emu2413, the `libretro.h`
header). The glue in `pico/`, the scripts and the harness in `tools/` are GPL-2.0-or-later, so the
blob as a whole is under GPL-2.0. Each file keeps its own notice. The blob also links zlib's
`crc32.c` (zlib licence), parts of newlib's C library and libm (BSD style licences) and libgcc's
helpers (GCC Runtime Library Exception); `THIRD_PARTY_NOTICES.md` lists everything. The GPL asks
that whoever receives the blob can get its complete source: every release carries this repository
with both submodules at the commits the blob was built from.

## Layout

| Path | What it is |
| --- | --- |
| `upstream/` | armNES, as a git submodule at v1.2.1 (`4e6264a`), never modified in place |
| `patches/` | changes to upstream, applied by `build.sh` to a copy in `out/src` |
| `zlib/` | zlib, as a git submodule at v1.3.1; only `crc32.c` is built |
| `pico/pico_core.c` | the LK's core interface (`nes_core_abi.h`, version 1) filled in for armNES's libretro port |
| `pico/pico_libc.c` | the heap over the LK's arena, the Disk System BIOS served to `fopen` from memory, newlib's system calls, the archive calls the port references |
| `pico/nes_core_abi.h` | a copy of `lk-mt6785/emu/nes/nes_core_abi.h`; the LK's copy is the reference |
| `pico/blob.ld` | link script: flat image at 0x4F800000, 20 byte header |
| `build.sh` | builds `out/armnes-pico.blob` |
| `tools/harness.c`, `tools/build_harness.sh` | runs the blob on a PC under qemu-arm, as the LK does |
| `tools/nescompare.c` | runs armNES's core natively and prints what two builds must agree on (per frame picture and sound CRCs, state CRCs) |
| `tools/make_test_images.py` | writes a tiny cartridge, disk and BIOS stand in, so the harness runs without games |
| `tools/package.sh` | packs a release: the SD card zip and the source archive, into `dist/` |
| `.github/workflows/build.yml` | CI: builds every push, runs the harness, publishes a release for a `v*` tag |
| `THIRD_PARTY_NOTICES.md` | what is in the blob and under which licence |

### Patches

`build.sh` copies upstream to `out/src` and applies `patches/*.patch` there, each behind a define of
its own that `build.sh` turns on (`DEFS`); without the define, upstream's code is unchanged.

There are none at the moment. The one this build carried up to armNES 1.1 (`0001-fds-batched-cycles`,
the Disk System's board run in batches so a disk game costs about what a cartridge does) is upstream
since armNES 1.2 ("Disk System: runs about as fast as a cartridge"), done there in its own way.

What looked like it might need a patch did not:

| Need | How it is met without a patch |
| --- | --- |
| Games as buffers | `retro_load_game` gets the image with no path; the port takes it from memory as for any frontend that loads content itself. It keeps its own copy, so the LK's buffer is free again after `load`. |
| The Disk System BIOS as a buffer | The port opens `disksys.rom` in the frontend's system folder. The glue answers GET_SYSTEM_DIRECTORY with `/pico`, and during `load` newlib's `_open` serves `/pico/disksys.rom` from the LK's buffer (`pico_libc.c`). Every other open fails, so the custom palette (`nes.pal`) and the Datach barcode list are simply absent. |
| No archive readers | The port's `.zip` and `.7z` code (`libretro/deps`) is not built. `pico_libc.c` answers its four calls: no path is ever an archive, since the LK unpacks a `.zip` itself. MAME VS. System and PlayChoice-10 sets therefore cannot be loaded. |
| zlib | armNES calls only `crc32()`, which comes from zlib's own `crc32.c`, unmodified. |
| Skipping the picture | armNES has its own: a frame the frontend will not show (GET_AUDIO_VIDEO_ENABLE without the video bit) runs with `skipPixels`, which leaves out what only the picture needs and still draws a sprite 0 hit the game is waiting for. |
| The 6502's program counter for `dbg_pc` | The machine is a static inside the port, but its system RAM (RETRO_MEMORY_SYSTEM_RAM) is a member of it, so the glue finds the machine from that. |

## Building

Needs `arm-none-eabi-gcc` with newlib (tested with 10.3-2021.07), python3 and git.

```
git clone --recurse-submodules <this repository>   # or: git submodule update --init upstream zlib
./build.sh
```

Output: `out/armnes-pico.blob`, plus the ELF and a link map. The script prints the size, how much
of the 8 MiB core slot it takes, and its SHA-256. (armNES's own submodule `armulators` is only
needed for its standalone app and is never fetched.)

Build settings, chosen for speed on the device: ARMv8-A in 32 bit ARM state, tuned for the
Cortex-A55 (the LK runs on CPU 0 of the MT6785, an in order A55), NEON and hardware floating point
(softfp calling convention, as the LK's own NEON code and the armGBA blob use), `-O3` and link time
optimisation, functions aligned to 64 bytes as upstream's CMake build does. The sources are the
ones upstream's `armnes_libretro` target builds: the core with FCEUmm's boards, the libretro port
and its NTSC filter, the options helper. For experiments `TUNE` (default `cortex-a55`), `OPT`
(default `-O3`) and `EXTRA_CFLAGS` override the flags. Releases use the defaults.

## Releases and CI

`.github/workflows/build.yml` builds the blob on Ubuntu 22.04 (its `gcc-arm-none-eabi` is the
10.3-2021.07 toolchain the blob is developed with) for every push and pull request, runs the
harness on the images `tools/make_test_images.py` writes, and keeps the packaged result as a
workflow artifact. Pushing a tag named `v*` also publishes a GitHub release with:

- `armnes-pico-<version>.zip`: `cores/armnes-pico.blob` (copy the `cores` folder to the root of the
  SD card), its SHA-256, `BUILD_INFO.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md` and `licenses/`;
- `armnes-pico-<version>-src.tar.gz`: this repository with both submodules at the commits the blob
  was built from (the complete source the GPL asks to go with the blob);
- `armnes-pico.blob` on its own.

Locally the same package comes from `./build.sh && tools/package.sh`.

## Testing on a PC

`tools/harness.c` maps the blob at its address and an arena at 0x50000000 and drives it through
the export table only, as the LK does, under qemu-arm:

```
tools/build_harness.sh        # needs arm-linux-gnueabihf-gcc
qemu-arm-static out/harness out/armnes-pico.blob game.nes 1200 last.ppm
```

It prints a picture hash every 60 frames, the sound frames per frame (798 or 799 at 48 kHz and
60.0988 Hz), `fps_milli`, `aspect_x1000`, the state and battery save sizes and the heap's use, then
checks:

- a state saved and loaded back replays the same 60 frames, picture and sound, and a short state
  is refused;
- frames run with `set_av_skip(1, 1)` (three in four over 120 frames) hand back no picture and no
  sound, the frames drawn between them are the same as when every frame is drawn, and so is the
  whole machine state at the end;
- run ahead's pair (mark, save, two look-ahead frames, load, reset) leaves the committed frames
  and the heap exactly as without it;
- an option set while the game runs applies at the next `run`.

| Variable | What it does |
| --- | --- |
| `HARNESS_BIOS=file` | the Disk System BIOS handed to `load` |
| `HARNESS_DISK=1` | `load`'s `is_disk` |
| `HARNESS_OPT="key=value,..."` | options set before `load` (each `set_option` result is printed) |
| `HARNESS_KEYS="a-b:mask,..."` | joypad bits (RETRO_DEVICE_ID_JOYPAD_*) held on pad 0 over frames a to b |
| `HARNESS_SHOTS=n,m` | write those frames as PPM files next to the last one |
| `HARNESS_ARENA_MB` | the arena's size in MiB (44, as the LK's Genesis session) |
| `HARNESS_SRAM=file` | the battery save, written after the run |

Run on Super Mario Bros., Super Mario Bros. 3, The Legend of Zelda, Final Fantasy, Kirby's
Adventure, Castlevania III (MMC5) and a two sided disk with a stand in BIOS, 1200 frames each with
Start pressed now and then: every check passes, 798.6 sound frames a frame, `fps_milli` 60099
(50007 with the region set to PAL or Dendy).

`tools/nescompare.sh <game> [frames]` checks a patch against upstream: it builds armNES's core
natively twice, upstream as it is and `out/src` with the patches and their defines, runs both on the
game and compares every frame's picture and sound and a state CRC every 600 frames. The `CMP_*`
variables (`tools/nescompare.c`) set the BIOS, buttons, disk changes, register writes between frames
(`CMP_POKE`), run ahead's save, look-ahead frame and load before every frame (`CMP_RUNAHEAD`) and
where states are written.

## Installing

Copy `out/armnes-pico.blob` to the SD card as `/cores/armnes-pico.blob`. The LK's NES session loads
it from there; Disk System games also need the BIOS on the card, which the LK reads and passes to
`load`.

## How the LK uses it

- The header: magic `0x3153454E` ("NES1"), interface version 1, the entry offset, the image size
  and the total span with the BSS (about 6.1 MiB). The image runs at 0x4F800000, the Genesis core's
  slot, and must end before the arena at 0x50000000; the blob's own static buffers (two machines
  for the VS. DualSystem, the NTSC filter's tables and picture) are in that span.
- `heap_init` hands the core its arena. Everything that depends on the game is allocated there when
  it loads and nothing while it runs. The high water mark (`dbg_get(1)`) is what the game needs:
  about 0.25 MiB plus the cartridge's PRG and CHR (Kirby's Adventure, 768 KiB of ROM: 897 KiB), and
  for a UNIF image or a VS. DualSystem a second copy of the image while it loads; a two sided disk
  0.6 MiB. `unload` frees it all but a few hundred bytes of newlib's stdio; `heap_init` again
  before the next game starts clean.
- `load(rom, size, bios, bios_size, is_disk)`: the image as read from the card (`.nes`, `.unf`,
  `.fds`, `.nsf`), the BIOS (at least 8 KiB; a 16 byte header in front is fine, the last 8 KiB are
  used) or NULL. armNES tells a disk from its contents, and refuses one without a BIOS (-1).
- `run` is one `retro_run`: a RGB565 picture (256 wide, 224 high with the default crop of 8 lines
  at the top and the bottom) and 48 kHz stereo sound, 798 or 799 frames of it.
- `set_av_skip(1, x)` before a `run` makes the frame one armNES does not draw (its own pixel skip,
  `video` comes back NULL); a sprite 0 hit the game is waiting for, and anything a Zapper needs,
  is still drawn, so the game's timeline is the same. `set_av_skip(x, 1)` drops the frame's sound
  (`frames` 0); the APU runs as always. (0, 0) for a shown frame.
- `heap_mark` and `heap_reset` bracket run ahead's save and load; the core allocates nothing while
  saving, loading or running, so they only guard.
- Battery saves: `sram_ptr`/`sram_size` is the cartridge's battery RAM (8 KiB for most games), with
  the board's own RAM after it on the few boards that keep some, or the whole disk for a Disk
  System game (`sides * 80 KiB` or more). Write it before the first `run`. Save states: 146 to
  154 KiB for a cartridge, 202 KiB with MMC5, 338 KiB for a two sided disk.
- `fps_milli` is 60099 for NTSC and 50007 for PAL and Dendy, `aspect_x1000` follows the Aspect Ratio
  and crop options (1429 for the default 4:3 with 224 lines).
- `dbg_pc` is the 6502's program counter. `dbg_get`: 0 the heap's bytes in use, 1 its high water
  mark, 2 the arena's size, 3 the heap's top now, 4 frames run since `load`, 5 the last `run`'s
  sound frames.

### Options

`set_option(key, value)` takes armNES's own libretro keys and values (upstream's README lists them
all) and refuses anything else (-1). Values set before `load` count from the start; most also apply
at the next `run` when changed while the game runs.

| Key | Values | When |
| --- | --- | --- |
| `armnes_region` | `Auto`, `NTSC`, `PAL`, `Dendy` | at `load` only |
| `armnes_ppu` | `Fast`, `Balanced`, `Exact` | at `load` only |
| `armnes_aspect` | `4:3`, `8:7 PAR`, `1:1` | next `run` |
| `armnes_crop_vertical`, `armnes_crop_horizontal` | `0`, `4`, `8`, `12`, `16` (defaults 8 and 0) | next `run` |
| `armnes_palette` | `armNES NTSC` and the 23 palettes of upstream's list | next `run` |
| `armnes_filter`, `armnes_scanlines` | `Off`, `Composite`, `S-Video`; `Off`, `Light`, `Medium`, `Dark` | next `run`; the picture becomes 768 (less the crop) by 448 |
| `armnes_sprite_limit` | `8 per line`, `No limit` | next `run` |
| `armnes_overclock` | `Off`, `2x (vblank)`, `3x (vblank)`, `4x (vblank)`, `2x (after the picture)`, `3x (after the picture)` | next `run` |
| `armnes_fds_fast` | `Off`, `On` | next `run` |
| `armnes_turbo` | `Off`, `30 per second`, `20 per second`, `15 per second`, `10 per second`, `7.5 per second` | next `run` (X and Y are turbo A and B) |
| `armnes_volume_<channel>`, `armnes_pan_<channel>` | `0%` to `100%`; `Left`, `75% left` ... `Center` ... `Right` | next `run` |
| `armnes_jumper` | `Auto`, `0` to `23` | restarts a game that has one |

The pad: `read_buttons(port)` returns RETRO_DEVICE_ID_JOYPAD_* bits for ports 0 to 3 (3 and 4 through
the Four Score or the Famicom adapter when the game's header names one). L3 on pad 0 takes a disk
out and puts the next side in.

## Target notes for maintainers

- **The interface.** `pico/nes_core_abi.h` is the contract with the LK: `nes_core_exports` (what the
  LK calls) and `nes_core_imports` (what the LK gives the core). The version is written twice, in
  the header and as a literal in `pico/blob.ld`; change both together, and keep the copy here
  identical to the LK's.
- **NEON.** The blob uses NEON and VFP registers (softfp calling convention). The LK does not save
  them on a thread switch, so the thread that runs the core and the LK's other threads that use
  NEON (its display code) must strictly alternate, as they do for the armGBA blob. If that cannot
  be guaranteed for a session, build with `-mfloat-abi=soft` and no `-mfpu` instead.
- **The C library.** newlib's C library and libm are linked; `pico/pico_libc.c` provides the heap
  and the system calls. It is compiled outside LTO, since newlib's archive members reference its
  symbols only at the final link, and with `-fno-builtin`.
- **Files.** The blob opens none on its own. `_open` knows one path, `/pico/disksys.rom`, and only
  while `load` runs with a BIOS.
