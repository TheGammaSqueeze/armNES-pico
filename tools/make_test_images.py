#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 TheGammaSqueeze
# Writes three tiny images for the harness, so it runs without any game (CI):
#
#   test.nes       an NROM cartridge whose program turns the picture on and counts in a loop
#   test.fds       a two sided Disk System image with only the disk header blocks
#   stub_bios.rom  8 KiB standing in for disksys.rom: its vectors point at a loop
#
# None of them is a game or a BIOS; they only exercise the core's load paths, frames, sound
# buffers and save states. Usage: tools/make_test_images.py <output folder>
import os
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "out/test"
os.makedirs(out, exist_ok=True)

# NROM-128: 16 KiB of program at $C000 (mirrored at $8000), 8 KiB of tiles
prg = bytearray(0x4000)
code = bytes([
    0x78,             # sei
    0xA9, 0x3F,       # lda #$3F      palette address
    0x8D, 0x06, 0x20, # sta $2006
    0xA9, 0x00,       # lda #$00
    0x8D, 0x06, 0x20, # sta $2006
    0xA9, 0x21,       # lda #$21      a blue backdrop
    0x8D, 0x07, 0x20, # sta $2007
    0xA9, 0x1E,       # lda #$1E      background and sprites on
    0x8D, 0x01, 0x20, # sta $2001
    0xE8,             # loop: inx
    0x8E, 0x00, 0x00, # stx $0000
    0x4C, 0x15, 0xC0, # jmp loop
])
prg[0:len(code)] = code
for vector in (0x3FFA, 0x3FFC, 0x3FFE):
    prg[vector], prg[vector + 1] = 0x00, 0xC0
chr_rom = bytes(0x2000)
header = b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8)
open(os.path.join(out, "test.nes"), "wb").write(header + prg + chr_rom)

bios = bytearray(0x2000)
bios[0:3] = bytes([0x4C, 0x00, 0xE0])   # jmp $E000
for vector in (0x1FFA, 0x1FFC, 0x1FFE):
    bios[vector], bios[vector + 1] = 0x00, 0xE0
open(os.path.join(out, "stub_bios.rom"), "wb").write(bios)

side = bytearray(65500)
side[0] = 1
side[1:15] = b"*NINTENDO-HVC*"
side[56] = 2                            # the file count block: no files
open(os.path.join(out, "test.fds"), "wb").write(b"FDS\x1a" + bytes([2]) + bytes(11) + bytes(side) * 2)
print(out)
