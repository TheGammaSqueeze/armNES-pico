/* armNES-pico host harness: runs out/armnes-pico.blob as the LK does, under qemu-arm.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 *
 * The blob is position dependent: it is mapped at the core slot (0x4F800000, 8 MiB) and given an
 * arena at 0x50000000, as the LK gives the Genesis core. The harness then drives it through the
 * export table only: heap_init, init, options, load, run, and checks what the LK relies on:
 *
 *   - frames come with a picture and about 800 stereo sound frames each (48 kHz at 60.0988 Hz);
 *   - fps_milli, aspect_x1000, av_info, the state and battery save sizes, the heap's use;
 *   - a state saved and loaded back replays the same frames, picture and sound;
 *   - frames run with set_av_skip(1, 1) leave the game's timeline as it was: after them, the
 *     picture and the whole machine state (retro_serialize) equal a run that drew every frame;
 *   - run ahead's pair: heap_mark, state_save, look-ahead frames, state_load, heap_reset leaves the
 *     heap where it was.
 *
 *   tools/build_harness.sh
 *   qemu-arm-static out/harness out/armnes-pico.blob game.nes [frames] [last.ppm]
 *
 * Environment:
 *   HARNESS_BIOS=file    the Disk System BIOS handed to load()
 *   HARNESS_DISK=1       load()'s is_disk
 *   HARNESS_OPT="k=v,..." options set before load (set_option's result is printed)
 *   HARNESS_KEYS="a-b:mask,..." joypad bits (RETRO_DEVICE_ID_JOYPAD_*) held on pad 0 over frames a..b
 *   HARNESS_SHOTS="n,m"  write those frames as <last>_<n>.ppm
 *   HARNESS_ARENA_MB     the arena's size (44, as the LK's Genesis session)
 *   HARNESS_SRAM=file    written with the battery save after the run
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "../pico/nes_core_abi.h"

#define SLOT_VMA  0x4F800000u
#define SLOT_SIZE 0x00800000u
#define ARENA_VMA 0x50000000u

static const struct nes_core_exports *g;
static int s_frame;

/* HARNESS_KEYS: "first-last:mask,..." held on pad 0, frames counted from 0 */
static unsigned keys_for(int frame)
{
	const char *k = getenv("HARNESS_KEYS");
	unsigned m = 0;
	while (k && *k) {
		char *e;
		int a = (int) strtol(k, &e, 10), b = a;
		k = e;
		if (*k == '-') { b = (int) strtol(k + 1, &e, 10); k = e; }
		if (*k == ':') { unsigned v = (unsigned) strtoul(k + 1, &e, 0); k = e; if (frame >= a && frame <= b) m |= v; }
		while (*k && *k != ',') k++;
		if (*k == ',') k++;
	}
	return m;
}

static unsigned read_buttons(unsigned port) { return port == 0 ? keys_for(s_frame) : 0; }
static long host_time(void) { return (long) time(NULL); }
static const struct nes_core_imports s_imports = { read_buttons, host_time };

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
	const uint8_t *b = p;
	while (n--) { h ^= *b++; h *= 16777619u; }
	return h;
}

struct result { uint32_t picture, sound; unsigned audio_frames; int drawn; };

static uint32_t picture_hash(const struct nes_frame *f)
{
	uint32_t h = 2166136261u;
	unsigned y;
	if (!f->video) return 0;
	for (y = 0; y < f->height; y++) h = fnv(h, (const uint8_t *) f->video + (size_t) y * f->pitch, f->width * 2u);
	return h;
}

static struct nes_frame s_last;
static unsigned long long s_audio_total;
static unsigned s_audio_min = ~0u, s_audio_max;

static struct result run_frame(void)
{
	struct nes_frame f;
	struct result r;
	memset(&f, 0, sizeof f);
	g->run(&f);
	s_frame++;
	r.picture = picture_hash(&f);
	r.sound = fnv(2166136261u, f.audio, (size_t) f.frames * 4u);
	r.audio_frames = f.frames;
	r.drawn = f.video != NULL;
	if (f.video) s_last = f;
	return r;
}

static void write_ppm(const char *path, const struct nes_frame *f)
{
	FILE *o = fopen(path, "wb");
	unsigned x, y;
	if (!o || !f->video) { if (o) fclose(o); return; }
	fprintf(o, "P6\n%u %u\n255\n", f->width, f->height);
	for (y = 0; y < f->height; y++) {
		const uint16_t *row = (const uint16_t *) ((const uint8_t *) f->video + (size_t) y * f->pitch);
		for (x = 0; x < f->width; x++) {
			unsigned c = row[x];
			unsigned char rgb[3] = { (unsigned char) ((c >> 11) << 3 | (c >> 13)), (unsigned char) (((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
						 (unsigned char) ((c & 31) << 3 | ((c >> 2) & 7)) };
			fwrite(rgb, 1, 3, o);
		}
	}
	fclose(o);
}

static void *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	void *b;
	long n;
	if (!f) { perror(path); exit(1); }
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	b = malloc((size_t) n + 1);
	if (fread(b, 1, (size_t) n, f) != (size_t) n) { perror("read"); exit(1); }
	fclose(f);
	*size = (size_t) n;
	return b;
}

static uint32_t state_hash(void)
{
	unsigned n = g->state_size();
	void *b = malloc(n);
	uint32_t h;
	if (g->state_save(b, n) != 0) { printf("state_save FAILED\n"); exit(1); }
	h = fnv(2166136261u, b, n);
	free(b);
	return h;
}

static int failures;
static void check(int ok, const char *what)
{
	printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

int main(int argc, char **argv)
{
	size_t bsz, romsz, biossz = 0;
	unsigned char *slot, *rom, *bios = NULL;
	const char *last = argc > 4 ? argv[4] : NULL;
	int frames = argc > 3 ? atoi(argv[3]) : 600;
	unsigned arena_mb = getenv("HARNESS_ARENA_MB") ? (unsigned) atoi(getenv("HARNESS_ARENA_MB")) : 44u;
	unsigned bw, bh, mw, mh, rate;
	int i;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 3) { fprintf(stderr, "usage: %s blob rom [frames] [last.ppm]\n", argv[0]); return 2; }
	slot = mmap((void *) SLOT_VMA, SLOT_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (slot == MAP_FAILED || mmap((void *) ARENA_VMA, arena_mb << 20, PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
		perror("mmap"); return 1;
	}
	/* the arena is not zeroed by the LK (its menu used it): fill it with junk so nothing relies on zeroes */
	memset((void *) ARENA_VMA, 0xA5, (size_t) arena_mb << 20);
	{
		unsigned char *b = slurp(argv[1], &bsz);
		unsigned *hdr = (unsigned *) b;
		if (hdr[0] != NES_CORE_ABI_MAGIC || hdr[1] != NES_CORE_ABI_VERSION || hdr[3] > bsz || hdr[4] > SLOT_SIZE || hdr[2] >= hdr[3]) {
			fprintf(stderr, "bad blob header\n"); return 1;
		}
		memcpy(slot, b, hdr[3]);
		memset(slot + hdr[3], 0, hdr[4] - hdr[3]);
		__builtin___clear_cache((char *) slot, (char *) slot + hdr[4]);
		g = ((nes_core_blob_init_fn) (void *) (slot + hdr[2]))(&s_imports);
		printf("blob: %zu bytes, span %u (%.2f MiB of 8)\n", bsz, hdr[4], hdr[4] / 1048576.0);
	}
	if (!g || g->magic != NES_CORE_ABI_MAGIC || g->version != NES_CORE_ABI_VERSION) { fprintf(stderr, "init rejected\n"); return 1; }

	g->heap_init((void *) ARENA_VMA, arena_mb << 20);
	g->init();
	{	/* HARNESS_OPT="key=value,..." */
		const char *o = getenv("HARNESS_OPT");
		char buf[1024], *save = NULL, *t;
		if (o) {
			snprintf(buf, sizeof buf, "%s", o);
			for (t = strtok_r(buf, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
				char *eq = strchr(t, '=');
				if (!eq) continue;
				*eq = 0;
				printf("set_option %s = %s -> %d\n", t, eq + 1, g->set_option(t, eq + 1));
			}
		}
	}
	rom = slurp(argv[2], &romsz);
	if (getenv("HARNESS_BIOS")) bios = slurp(getenv("HARNESS_BIOS"), &biossz);
	{
		int rc = g->load(rom, (unsigned) romsz, bios, (unsigned) biossz, getenv("HARNESS_DISK") ? atoi(getenv("HARNESS_DISK")) : 0);
		printf("load: %d (heap in use %u KiB, peak %u KiB)\n", rc, (g->dbg_get(0) + 1023u) / 1024u, (g->dbg_get(1) + 1023u) / 1024u);
		if (rc != 0) return 3;
	}
	/* the LK's buffer is its own again after load: wipe it, the core must have kept a copy */
	memset(rom, 0x5A, romsz);
	g->av_info(&bw, &bh, &mw, &mh, &rate);
	printf("av_info: base %ux%u max %ux%u rate %u, fps_milli %u, aspect_x1000 %u\n", bw, bh, mw, mh, rate, g->fps_milli(), g->aspect_x1000());
	printf("state %u bytes, sram %u bytes\n", g->state_size(), g->sram_size());

	{	/* the plain run */
		const char *shots = getenv("HARNESS_SHOTS");
		int next = shots ? atoi(shots) : -1;
		for (i = 0; i < frames; i++) {
			struct result r = run_frame();
			s_audio_total += r.audio_frames;
			if (i >= 2) {   /* the first frames after power on are short */
				if (r.audio_frames < s_audio_min) s_audio_min = r.audio_frames;
				if (r.audio_frames > s_audio_max) s_audio_max = r.audio_frames;
			}
			if ((i + 1) % 60 == 0)
				printf("frame %4d picture %08x %ux%u pitch %u pc %04x audio %u\n", i + 1, r.picture, s_last.width, s_last.height,
				       s_last.pitch, g->dbg_pc(), r.audio_frames);
			if (shots && i + 1 == next && last) {
				char nb[512];
				snprintf(nb, sizeof nb, "%.*s_%d.ppm", (int) strlen(last) - 4, last, next);
				write_ppm(nb, &s_last);
				shots = strchr(shots, ',');
				next = shots ? atoi(++shots) : -1;
			}
		}
		printf("audio: %llu stereo frames over %d frames = %.2f a frame (min %u max %u); 48000 / 60.0988 = %.2f\n",
		       s_audio_total, frames, (double) s_audio_total / frames, s_audio_min, s_audio_max, 48000.0 / 60.0988);
		if (last) write_ppm(last, &s_last);
	}

	{	/* save, 60 frames, load, the same 60 frames: identical picture and sound every frame */
		unsigned n = g->state_size();
		void *st = malloc(n);
		struct result a[60], b[60];
		int same = 1;
		void *mark = g->heap_mark();
		unsigned used = g->dbg_get(0), top = g->dbg_get(3);
		check(g->state_save(st, n) == 0, "state_save");
		for (i = 0; i < 60; i++) a[i] = run_frame();
		check(g->state_load(st, n) == 0, "state_load");
		g->heap_reset(mark);
		check(g->dbg_get(0) == used && g->dbg_get(3) == top, "heap back at the mark after the save and load pair");
		for (i = 0; i < 60; i++) {
			b[i] = run_frame();
			if (a[i].picture != b[i].picture || a[i].sound != b[i].sound) same = 0;
		}
		printf("state round trip: frame 60 picture %08x / %08x, sound %08x / %08x\n", a[59].picture, b[59].picture, a[59].sound, b[59].sound);
		check(same, "state round trip: 60 frames of picture and sound identical");
		/* armNES 1.2 takes a state a little short (1.1's were), so only a cut one is refused */
		check(g->state_load(st, n / 2) != 0, "a state cut in half is refused");
		free(st);
	}

	{	/* skipped frames: from one state, 120 frames drawn every time against the same 120 with only
		 * every 4th drawn and the rest set_av_skip(1, 1): the drawn frames' pictures and the whole
		 * machine state at the end must match */
		unsigned n = g->state_size();
		void *st = malloc(n);
		uint32_t pa[120], pb[120], sa, sb;
		int same = 1, skipped_null = 1, skipped_silent = 1;
		g->state_save(st, n);
		for (i = 0; i < 120; i++) pa[i] = run_frame().picture;
		sa = state_hash();
		g->state_load(st, n);
		for (i = 0; i < 120; i++) {
			int skip = (i % 4) != 3;
			struct result r;
			g->set_av_skip(skip, skip);
			r = run_frame();
			g->set_av_skip(0, 0);
			pb[i] = r.picture;
			if (skip && r.drawn) skipped_null = 0;
			if (skip && r.audio_frames) skipped_silent = 0;
			if (!skip && pa[i] != pb[i]) same = 0;
		}
		sb = state_hash();
		printf("skip run: frame 120 picture %08x / %08x, state %08x / %08x\n", pa[119], pb[119], sa, sb);
		check(skipped_null, "skipped frames hand back no picture");
		check(skipped_silent, "skipped frames hand back no sound");
		check(same, "drawn frames between skipped ones identical");
		check(sa == sb, "machine state after skipped frames identical");
		free(st);
	}

	{	/* run ahead: per committed frame, mark, save, 2 look-ahead frames (render of the first
		 * skipped, sound of both), load, reset to the mark. The committed frames must equal a plain
		 * run's. */
		unsigned n = g->state_size();
		void *st = malloc(n), *ra = malloc(n);
		uint32_t plain[60], ahead[60];
		unsigned used, top;
		int same = 1;
		g->state_save(st, n);
		for (i = 0; i < 60; i++) plain[i] = run_frame().picture;
		g->state_load(st, n);
		used = g->dbg_get(0);
		top = g->dbg_get(3);
		for (i = 0; i < 60; i++) {
			void *mark;
			ahead[i] = run_frame().picture;
			mark = g->heap_mark();
			g->state_save(ra, n);
			g->set_av_skip(1, 1); run_frame();
			g->set_av_skip(0, 1); run_frame();
			g->set_av_skip(0, 0);
			g->state_load(ra, n);
			g->heap_reset(mark);
			if (ahead[i] != plain[i]) same = 0;
		}
		check(same, "run ahead: committed frames identical to a plain run");
		check(g->dbg_get(0) == used && g->dbg_get(3) == top, "run ahead: the heap does not grow");
		free(st); free(ra);
	}

	{	/* an option changed while the game runs applies from the next run(): the crop */
		struct result r;
		unsigned bw2, bh2;
		int taken = g->set_option("armnes_crop_vertical", "16") == 0;
		r = run_frame();
		g->av_info(&bw2, &bh2, NULL, NULL, NULL);
		printf("crop 16: frame %ux%u, av_info %ux%u, aspect_x1000 %u\n", s_last.width, s_last.height, bw2, bh2, g->aspect_x1000());
		check(taken && r.drawn && s_last.height == bh2 && (bh2 == 208 || bh2 == 416), "an option set while running applies at the next run()");
		g->set_option("armnes_crop_vertical", "8");
		run_frame();
	}
	printf("heap: in use %u KiB, peak %u KiB of %u KiB; frames run %u\n", (g->dbg_get(0) + 1023u) / 1024u, (g->dbg_get(1) + 1023u) / 1024u,
	       g->dbg_get(2) / 1024u, g->dbg_get(4));
	if (getenv("HARNESS_SRAM") && g->sram_size()) {
		FILE *o = fopen(getenv("HARNESS_SRAM"), "wb");
		fwrite(g->sram_ptr(), 1, g->sram_size(), o);
		fclose(o);
	}
	g->unload();
	printf("unload: heap in use %u bytes\n", g->dbg_get(0));
	printf("%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
	return failures ? 4 : 0;
}
