/* armNES-pico: the LK's NES core interface (nes_core_abi.h) over armNES's libretro port.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze
 *
 * The blob is armNES's own libretro port (upstream libretro/libretro.c) with the frontend's side of
 * libretro played here: the environment, picture, sound and input callbacks are installed at init,
 * and each run() is one retro_run(). What the LK and libretro.c each expect, and how this file meets
 * them:
 *
 *   - Games come as buffers: retro_load_game gets the image with no path, and libretro.c reads it
 *     from memory as it does for any frontend that loads content itself.
 *   - The Disk System BIOS comes as a buffer too, but libretro.c opens disksys.rom in the system
 *     folder. The environment answers GET_SYSTEM_DIRECTORY with a folder of our own, and while
 *     load() runs, newlib's fopen of that one file is served from the buffer (pico_libc.c).
 *   - Options: set_option fills a table that GET_VARIABLE answers from, and GET_VARIABLE_UPDATE
 *     reports a change once, so libretro.c applies it at the end of the next frame. The port lists
 *     its options as version 2 definitions (names, value names, defaults), kept here for the LK's
 *     Core Settings page (opt_count / opt_info); set_option takes only keys and values found there.
 *   - Frames the LK will not show: GET_AUDIO_VIDEO_ENABLE without the video bit makes libretro.c run
 *     the frame with armNES's own pixel skip (the fast PPU leaves out only what the picture needs;
 *     a sprite 0 hit the game polls for is still drawn), and with GET_CAN_DUPE it hands back no
 *     picture. Skipped sound is dropped here; the APU runs as always.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libretro.h"
#include "nes.h"
#include "nes_core_abi.h"
#include "pico_libc.h"

/* ---- libretro.c ---- */
void     retro_set_environment(retro_environment_t);
void     retro_set_video_refresh(retro_video_refresh_t);
void     retro_set_audio_sample(retro_audio_sample_t);
void     retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void     retro_set_input_poll(retro_input_poll_t);
void     retro_set_input_state(retro_input_state_t);
void     retro_init(void);
void     retro_deinit(void);
bool     retro_load_game(const struct retro_game_info *);
void     retro_unload_game(void);
void     retro_run(void);
void     retro_reset(void);
void     retro_get_system_av_info(struct retro_system_av_info *);
void    *retro_get_memory_data(unsigned);
size_t   retro_get_memory_size(unsigned);
size_t   retro_serialize_size(void);
bool     retro_serialize(void *, size_t);
bool     retro_unserialize(const void *, size_t);

/* The system folder libretro.c is told about, and the one file in it */
#define PICO_SYSTEM_DIR "/pico"
#define PICO_BIOS_PATH  PICO_SYSTEM_DIR "/disksys.rom"
#define PICO_BIOS_BYTES 0x2000u

static const struct nes_core_imports *g_imp;

long pico_host_seconds(void) { return g_imp && g_imp->host_time ? g_imp->host_time() : 0; }

/* ---- options ---- */

#define OPT_MAX 80
static struct { char key[40]; char value[40]; } g_opt[OPT_MAX];
static unsigned g_opt_count;
static bool g_opt_changed;
/* the port's own list: version 2 definitions (static in the port), or "Label; default|other|..."
 * from SET_VARIABLES when a frontend has no version 2 */
static const struct retro_variable *g_vars;
static const struct retro_core_options_v2 *g_defs;

static void copy_str(char *d, const char *s, size_t cap)
{
	size_t i = 0;
	for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
	d[i] = 0;
}

/* whether value is one of key's values in the port's list (true while there is no list yet) */
static bool option_valid(const char *key, const char *value)
{
	const struct retro_variable *v;
	if (g_defs && g_defs->definitions) {
		const struct retro_core_option_v2_definition *d;
		for (d = g_defs->definitions; d->key; d++) {
			unsigned k;
			if (strcmp(d->key, key)) continue;
			for (k = 0; k < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[k].value; k++)
				if (!strcmp(d->values[k].value, value)) return true;
			return false;
		}
		return false;
	}
	if (!g_vars) return true;
	for (v = g_vars; v->key; v++) {
		const char *p;
		size_t n = strlen(value);
		if (strcmp(v->key, key) || !v->value) continue;
		p = strstr(v->value, "; ");
		if (!p) return false;
		p += 2;
		for (;;) {
			const char *bar = strchr(p, '|');
			size_t len = bar ? (size_t) (bar - p) : strlen(p);
			if (len == n && !memcmp(p, value, n)) return true;
			if (!bar) return false;
			p = bar + 1;
		}
	}
	return false;
}

/* ---- the core's own options, for the LK's Core Settings page ----
 * The definitions as struct pico_core_option (pointers into the port's constant tables), less the
 * options the LK sets itself and the ones these consoles cannot use. */
#define COPT_MAX   96
#define COPT_VALS  2048
static struct pico_core_option g_copt[COPT_MAX];
static const char *g_copt_val[COPT_VALS], *g_copt_lab[COPT_VALS];
static int g_copt_n, g_copt_vn;

static bool starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }
static bool copt_hidden(const char *key)
{
	return !strcmp(key, "armnes_region") || !strcmp(key, "armnes_aspect") ||	/* the LK's own rows */
	       starts(key, "armnes_cart_code_") || !strcmp(key, "armnes_game_genie") ||	/* cheats: typed codes */
	       !strcmp(key, "armnes_par_game") ||
	       !strcmp(key, "armnes_left_stick_dpad") || !strcmp(key, "armnes_stick_invert") ||	/* the LK maps the pad */
	       starts(key, "armnes_motion_") || !strcmp(key, "armnes_four_players") ||
	       starts(key, "armnes_zapper_") ||					/* no light gun */
	       !strcmp(key, "armnes_expansion");					/* no such devices */
}

static void copt_take(const struct retro_core_options_v2 *o)
{
	const struct retro_core_option_v2_definition *d;
	g_defs = o;
	g_copt_n = g_copt_vn = 0;
	if (!o || !o->definitions) return;
	for (d = o->definitions; d->key && g_copt_n < COPT_MAX; d++) {
		struct pico_core_option *e = &g_copt[g_copt_n];
		int nv = 0, k;
		if (copt_hidden(d->key)) continue;
		while (nv < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[nv].value) nv++;
		if (!nv || g_copt_vn + nv > COPT_VALS) continue;
		e->key = d->key;
		e->label = d->desc ? d->desc : d->key;
		e->category = 0;
		if (d->category_key && o->categories) {
			const struct retro_core_option_v2_category *c;
			for (c = o->categories; c->key; c++)
				if (!strcmp(c->key, d->category_key)) { e->category = c->desc; break; }
		}
		e->values = &g_copt_val[g_copt_vn];
		e->labels = &g_copt_lab[g_copt_vn];
		e->nvalues = nv;
		e->def = 0;
		for (k = 0; k < nv; k++) {
			g_copt_val[g_copt_vn + k] = d->values[k].value;
			g_copt_lab[g_copt_vn + k] = d->values[k].label ? d->values[k].label : d->values[k].value;
			if (d->default_value && !strcmp(d->values[k].value, d->default_value)) e->def = k;
		}
		g_copt_vn += nv;
		g_copt_n++;
	}
}
static int nes_opt_count(void) { return g_copt_n; }
static const struct pico_core_option *nes_opt_info(int i) { return i >= 0 && i < g_copt_n ? &g_copt[i] : NULL; }

static int nes_set_option(const char *key, const char *value)
{
	unsigned i;
	if (!key || !value || strlen(key) >= sizeof g_opt[0].key || strlen(value) >= sizeof g_opt[0].value)
		return -1;
	if (!option_valid(key, value)) return -1;
	for (i = 0; i < g_opt_count; i++) {
		if (!strcmp(g_opt[i].key, key)) {
			if (strcmp(g_opt[i].value, value)) {
				copy_str(g_opt[i].value, value, sizeof g_opt[i].value);
				g_opt_changed = true;
			}
			return 0;
		}
	}
	if (g_opt_count >= OPT_MAX) return -1;
	copy_str(g_opt[g_opt_count].key, key, sizeof g_opt[0].key);
	copy_str(g_opt[g_opt_count].value, value, sizeof g_opt[0].value);
	g_opt_count++;
	g_opt_changed = true;
	return 0;
}

/* ---- the frame being run ---- */

#define AUDIO_MAX 4096u   /* stereo frames; one frame is about 800 at 48 kHz */
static int16_t g_audio[AUDIO_MAX * 2];
static unsigned g_audio_frames;
static const void *g_video;
static unsigned g_video_w, g_video_h, g_video_pitch;
static bool g_skip_video, g_skip_audio;
static unsigned g_frames_run;

/* ---- libretro callbacks ---- */

/* the memory map the port announces (RETRO_ENVIRONMENT_SET_MEMORY_MAPS), copied: its array need
 * not outlive the call. For RetroAchievements (get_memory). */
#define MEM_DESCS 16
static struct retro_memory_descriptor g_mem_desc[MEM_DESCS];
static unsigned g_mem_ndesc;

static bool env_cb(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return data && *(const enum retro_pixel_format *) data == RETRO_PIXEL_FORMAT_RGB565;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		if (!data) return false;
		*(const char **) data = PICO_SYSTEM_DIR;
		return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES:
		g_vars = (const struct retro_variable *) data;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		if (!data) return false;
		*(unsigned *) data = 2;		/* the definitions, with their value names and defaults */
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
		copt_take((const struct retro_core_options_v2 *) data);
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = (struct retro_variable *) data;
		unsigned i;
		if (!v || !v->key) return false;
		v->value = NULL;
		for (i = 0; i < g_opt_count; i++) {
			if (!strcmp(g_opt[i].key, v->key)) { v->value = g_opt[i].value; return true; }
		}
		return false;   /* not set: the port's default */
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		if (!data) return false;
		*(bool *) data = g_opt_changed;
		g_opt_changed = false;
		return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
		if (!data) return false;
		*(int *) data = (g_skip_video ? 0 : RETRO_AV_ENABLE_VIDEO) | (g_skip_audio ? 0 : RETRO_AV_ENABLE_AUDIO);
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		if (!data) return false;
		*(bool *) data = true;
		return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
	case RETRO_ENVIRONMENT_SET_MESSAGE:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
		return true;    /* noted; the LK reads geometry and timing back through av_info */
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
		const struct retro_memory_map *m = (const struct retro_memory_map *) data;
		g_mem_ndesc = 0;
		if (m && m->descriptors)
			for (; g_mem_ndesc < m->num_descriptors && g_mem_ndesc < MEM_DESCS; g_mem_ndesc++)
				g_mem_desc[g_mem_ndesc] = m->descriptors[g_mem_ndesc];
		return true;
	}
	default:
		return false;   /* everything else declined: no option definitions, sensors, fast forward */
	}
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
	g_video = data;
	g_video_w = width;
	g_video_h = height;
	g_video_pitch = (unsigned) pitch;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
	size_t room, n;
	if (g_skip_audio) return frames;
	room = AUDIO_MAX - g_audio_frames;
	n = frames < room ? frames : room;
	memcpy(g_audio + g_audio_frames * 2u, data, n * 2u * sizeof(int16_t));
	g_audio_frames += (unsigned) n;
	return frames;
}

static void audio_sample_cb(int16_t left, int16_t right)
{
	int16_t pair[2] = { left, right };
	audio_batch_cb(pair, 1);
}

static void input_poll_cb(void) { }

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	unsigned buttons;
	(void) index;
	if (device != RETRO_DEVICE_JOYPAD || port > 3 || !g_imp || !g_imp->read_buttons) return 0;
	buttons = g_imp->read_buttons(port);
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t) (buttons & 0xFFFFu);
	return id < 16 ? (int16_t) ((buttons >> id) & 1u) : 0;
}

/* ---- exports ---- */

static void nes_heap_init(void *base, unsigned size) { pico_heap_init(base, size); }

static void nes_init(void)
{
	g_skip_video = g_skip_audio = false;
	g_frames_run = 0;
	retro_set_environment(env_cb);
	retro_set_video_refresh(video_cb);
	retro_set_audio_sample(audio_sample_cb);
	retro_set_audio_sample_batch(audio_batch_cb);
	retro_set_input_poll(input_poll_cb);
	retro_set_input_state(input_state_cb);
	retro_init();
}

static int nes_load(const void *rom, unsigned size, const void *bios, unsigned bios_size, int is_disk)
{
	struct retro_game_info info;
	bool ok;
	(void) is_disk;   /* armNES tells a disk from its contents; a disk without a BIOS is refused */
	if (!rom || !size) return -1;
	if (bios && bios_size >= PICO_BIOS_BYTES) pico_memfile_set(PICO_BIOS_PATH, bios, bios_size);
	memset(&info, 0, sizeof info);
	info.path = NULL;
	info.data = rom;
	info.size = size;
	ok = retro_load_game(&info);
	pico_memfile_set(NULL, NULL, 0);   /* the port keeps its own copy of the BIOS */
	g_frames_run = 0;
	return ok ? 0 : -1;
}

static void nes_reset(void) { retro_reset(); }

static void nes_unload(void)
{
	retro_unload_game();
	retro_deinit();
}

static void nes_run(struct nes_frame *out)
{
	g_video = NULL;
	g_audio_frames = 0;
	retro_run();
	g_frames_run++;
	if (!out) return;
	out->video = g_video;
	out->width = g_video_w;
	out->height = g_video_h;
	out->pitch = g_video_pitch;
	out->audio = g_audio;
	out->frames = g_audio_frames;
}

static void nes_av_info(unsigned *base_w, unsigned *base_h, unsigned *max_w, unsigned *max_h, unsigned *sample_rate)
{
	struct retro_system_av_info av;
	retro_get_system_av_info(&av);
	if (base_w) *base_w = av.geometry.base_width;
	if (base_h) *base_h = av.geometry.base_height;
	if (max_w) *max_w = av.geometry.max_width;
	if (max_h) *max_h = av.geometry.max_height;
	if (sample_rate) *sample_rate = (unsigned) (av.timing.sample_rate + 0.5);
}

static unsigned nes_fps_milli(void)
{
	struct retro_system_av_info av;
	retro_get_system_av_info(&av);
	return (unsigned) (av.timing.fps * 1000.0 + 0.5);
}

static unsigned nes_aspect_x1000(void)
{
	struct retro_system_av_info av;
	float a;
	retro_get_system_av_info(&av);
	a = av.geometry.aspect_ratio;
	if (a <= 0.0f && av.geometry.base_height)
		a = (float) av.geometry.base_width / (float) av.geometry.base_height;
	return (unsigned) (a * 1000.0f + 0.5f);
}

static void *nes_sram_ptr(void) { return retro_get_memory_data(RETRO_MEMORY_SAVE_RAM); }
static unsigned nes_sram_size(void) { return (unsigned) retro_get_memory_size(RETRO_MEMORY_SAVE_RAM); }

static unsigned nes_state_size(void) { return (unsigned) retro_serialize_size(); }
static int nes_state_save(void *buf, unsigned size) { return buf && retro_serialize(buf, size) ? 0 : -1; }
static int nes_state_load(const void *buf, unsigned size) { return buf && retro_unserialize(buf, size) ? 0 : -1; }

static void nes_set_av_skip(int skip_video, int skip_audio)
{
	g_skip_video = skip_video != 0;
	g_skip_audio = skip_audio != 0;
}

/* The machine the port runs is static inside libretro.c; its system RAM is a member of it, so the
 * machine is found back from that. */
static const struct NES *nes_machine(void)
{
	const uint8_t *ram = (const uint8_t *) retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	return ram ? (const struct NES *) (ram - offsetof(struct NES, ram)) : NULL;
}

static unsigned nes_dbg_pc(void)
{
	const struct NES *m = nes_machine();
	return m ? m->cpu.pc : 0;
}

/* 0: heap bytes in use, 1: the heap's high water mark (the arena a game needs), 2: the arena's size,
 * 3: the heap's top now, 4: frames run since load, 5: the last run()'s audio frames */
static unsigned nes_dbg_get(unsigned idx)
{
	switch (idx) {
	case 0: return pico_heap_used();
	case 1: return pico_heap_peak();
	case 2: return pico_heap_size();
	case 3: return pico_heap_top();
	case 4: return g_frames_run;
	case 5: return g_audio_frames;
	default: return 0;
	}
}

/* 3: the game's memory for RetroAchievements: the port's blocks and the map it announced, its
 * descriptors laid out as pico_memdesc (pico_core_memory.h) */
_Static_assert(sizeof(struct pico_memdesc) == sizeof(struct retro_memory_descriptor), "pico_memdesc layout");
static void nes_get_memory(struct pico_core_memory *out)
{
	memset(out, 0, sizeof *out);
	out->system_ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	out->system_ram_size = out->system_ram ? (unsigned) retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
	out->save_ram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	out->save_ram_size = out->save_ram ? (unsigned) retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) : 0;
	out->descriptors = g_mem_ndesc ? (const struct pico_memdesc *) g_mem_desc : NULL;
	out->num_descriptors = g_mem_ndesc;
}

static const struct nes_core_exports g_exports = {
	NES_CORE_ABI_MAGIC,
	NES_CORE_ABI_V3,
	nes_heap_init,
	nes_init,
	nes_load,
	nes_reset,
	nes_unload,
	nes_run,
	nes_av_info,
	nes_fps_milli,
	nes_aspect_x1000,
	nes_sram_ptr,
	nes_sram_size,
	nes_state_size,
	nes_state_save,
	nes_state_load,
	nes_set_option,
	nes_set_av_skip,
	pico_heap_mark,
	pico_heap_reset,
	nes_dbg_pc,
	nes_dbg_get,
	nes_opt_count,
	nes_opt_info,
	nes_get_memory,
};

__attribute__((used, section(".text.nes_core_blob_init")))
const struct nes_core_exports *nes_core_blob_init(const struct nes_core_imports *imp)
{
	g_imp = imp;
	return &g_exports;
}
