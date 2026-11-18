/* armNES-pico: <dirent.h> for the freestanding blob.
 *
 * armNES's libretro port (1.2 on) lists the folder beside a game for the cheat cartridges' Game on
 * Top and RetroArch's cheat database. Inside the bootloader there is no folder to list: the LK hands
 * the core one game from memory. newlib has no directory reading for this target, so opendir always
 * finds nothing and the port carries on as with an empty folder.
 */
#ifndef ARMNES_PICO_DIRENT_H
#define ARMNES_PICO_DIRENT_H

struct dirent {
	char d_name[256];
};
typedef struct pico_dir DIR;

static inline DIR *opendir(const char *path) { (void)path; return (DIR *)0; }
static inline struct dirent *readdir(DIR *dir) { (void)dir; return (struct dirent *)0; }
static inline int closedir(DIR *dir) { (void)dir; return 0; }

#endif
