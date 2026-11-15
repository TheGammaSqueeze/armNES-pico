#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 TheGammaSqueeze
# Build the host harness (tools/harness.c) for qemu-arm: arm-linux-gnueabihf-gcc, static.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out
arm-linux-gnueabihf-gcc -O1 -g -static -Wall -o out/harness tools/harness.c
echo "out/harness: run with qemu-arm-static out/harness out/armnes-pico.blob <game> [frames] [last.ppm]"
