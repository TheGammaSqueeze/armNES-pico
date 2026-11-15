/* armNES-pico: a run of armNES's core on the PC that prints what two builds must agree on.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 *
 * Built natively with armNES's core twice, upstream as it is and out/src with the patches and
 * their defines, it prints for every frame the CRC-32 of the picture and of the sound, and every
 * CMP_STATE_EVERY frames the CRC-32 of the saved state (nesSaveState). Diffing the two outputs
 * shows the first frame where the builds part; tools/nescompare.sh builds both and diffs them.
 *
 *   nescompare <game> [frames]
 *
 * Environment:
 *   CMP_BIOS=file          the Disk System BIOS
 *   CMP_PRESS="frame:mask[:frames],..."  pad 1 buttons held (bit 0 A ... bit 7 Right), 4 frames
 *                          unless given
 *   CMP_DISK="frame:side,..."  the disk side put in before that frame (-1 takes it out)
 *   CMP_POKE="frame:address:value,..."  CPU writes made before that frame, in order (to drive a
 *                          board's registers, such as the Disk System's sound, that the game
 *                          leaves alone)
 *   CMP_STATE_EVERY=n      a state CRC every n frames (default 600; 0 none)
 *   CMP_STATE_DIR=dir      also write each of those states there, as state_<frame>.bin
 *   CMP_SYNC=1             bring the PPU up to the CPU before each of those saves (both builds must
 *                          then agree on where the PPU is, not only on what it will do)
 *   CMP_RUNAHEAD=1         run ahead's pattern before every frame but the first: save, one
 *                          look-ahead frame, load; the frames printed must equal a run without it.
 *                          (Not before the first: armNES leaves the reset sequence's cycles
 *                          uncounted until the first frame settles them, and a state saved before
 *                          that loses them, on every board.)
 */
#include "nes.h"
#include "board.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = (size_t) ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *data = malloc(*size ? *size : 1);
	if (data && fread(data, 1, *size, f) != *size) {
		free(data);
		data = NULL;
	}
	fclose(f);
	return data;
}

/* "frame:a[:b],..." entries that start at this frame or cover it */
static uint8_t pad_for(const char *spec, int frame)
{
	uint8_t m = 0;
	for (const char *p = spec; p && *p;) {
		int at = 0, mask = 0, hold = 4;
		if (sscanf(p, "%d:%i:%d", &at, &mask, &hold) >= 2 && frame >= at && frame < at + hold)
			m |= (uint8_t) mask;
		p = strchr(p, ',');
		p = p ? p + 1 : NULL;
	}
	return m;
}

static void pokes_for(struct NES *nes, const char *spec, int frame)
{
	for (const char *p = spec; p && *p;) {
		int at = 0, address = 0, value = 0;
		if (sscanf(p, "%d:%i:%i", &at, &address, &value) == 3 && at == frame)
			busWrite(nes, (uint16_t) address, (uint8_t) value);
		p = strchr(p, ',');
		p = p ? p + 1 : NULL;
	}
}

static int disk_for(const char *spec, int frame, int *side)
{
	for (const char *p = spec; p && *p;) {
		int at = 0, s = 0;
		if (sscanf(p, "%d:%d", &at, &s) == 2 && at == frame) {
			*side = s;
			return 1;
		}
		p = strchr(p, ',');
		p = p ? p + 1 : NULL;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: nescompare <game> [frames]\n");
		return 2;
	}
	size_t size, bios_size = 0;
	uint8_t *data = read_file(argv[1], &size);
	if (!data) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	int frames = argc > 2 ? atoi(argv[2]) : 3600;
	const char *press = getenv("CMP_PRESS"), *disk = getenv("CMP_DISK"), *poke = getenv("CMP_POKE");
	const char *state_dir = getenv("CMP_STATE_DIR");
	int every = getenv("CMP_STATE_EVERY") ? atoi(getenv("CMP_STATE_EVERY")) : 600;
	int sync = getenv("CMP_SYNC") && atoi(getenv("CMP_SYNC"));
	int runahead = getenv("CMP_RUNAHEAD") && atoi(getenv("CMP_RUNAHEAD"));

	static struct NES nes;
	static int16_t audio[16384];
	nesInit(&nes);
	nes.audioOut = audio;
	nes.audioCapacity = sizeof(audio) / sizeof(audio[0]);
	uint8_t *bios = getenv("CMP_BIOS") ? read_file(getenv("CMP_BIOS"), &bios_size) : NULL;
	if (bios && bios_size >= 0x2000)
		nes.fdsBios = bios + bios_size - 0x2000;
	if (!nesLoad(&nes, data, size)) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 3;
	}
	size_t state_size = nesStateSize(&nes);
	uint8_t *state = malloc(state_size), *ahead = malloc(state_size);
	uint32_t all_video = 0, all_audio = 0;
	unsigned long samples = 0;

	for (int frame = 0; frame < frames; frame++) {
		int side;
		if (disk_for(disk, frame, &side))
			fdsInsert(&nes, side);
		pokes_for(&nes, poke, frame);
		nes.pads[0] = pad_for(press, frame);
		if (runahead && frame > 0) {
			if (!nesSaveState(&nes, ahead, state_size)) {
				fprintf(stderr, "save failed at %d\n", frame);
				return 4;
			}
			nesRunFrame(&nes);
			if (!nesLoadState(&nes, ahead, state_size)) {
				fprintf(stderr, "load failed at %d\n", frame);
				return 4;
			}
		}
		nesRunFrame(&nes);
		uint32_t v = (uint32_t) crc32(0, (const uint8_t *) nes.frameBuffer, NES_WIDTH * NES_HEIGHT * 2);
		uint32_t a = (uint32_t) crc32(0, (const uint8_t *) nes.audioOut, (uInt) (nes.audioCount * 2));
		all_video = (uint32_t) crc32(all_video, (const uint8_t *) &v, 4);
		all_audio = (uint32_t) crc32(all_audio, (const uint8_t *) nes.audioOut, (uInt) (nes.audioCount * 2));
		samples += (unsigned long) nes.audioCount;
		printf("%d video %08x audio %08x n %u pc %04x\n", frame, v, a, (unsigned) nes.audioCount, nes.cpu.pc);
		if (every > 0 && (frame + 1) % every == 0) {
			if (sync)
				nesPpuSync(&nes);
			if (!nesSaveState(&nes, state, state_size)) {
				fprintf(stderr, "save failed at %d\n", frame);
				return 4;
			}
			printf("%d state %08x size %zu\n", frame, (uint32_t) crc32(0, state, (uInt) state_size), state_size);
			if (state_dir) {
				char path[512];
				snprintf(path, sizeof(path), "%s/state_%d.bin", state_dir, frame);
				FILE *f = fopen(path, "wb");
				if (f) {
					fwrite(state, 1, state_size, f);
					fclose(f);
				}
			}
		}
	}
	printf("total video %08x audio %08x samples %lu\n", all_video, all_audio, samples);
	return 0;
}
