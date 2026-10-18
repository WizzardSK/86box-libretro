/*
 * 86Box libretro core: what the core's files share.
 */
#ifndef LR_CORE_H
#define LR_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* <system>/86box/, with a trailing slash: the user data, ROMs (roms/) and assets */
extern const char *lr_data_dir(void);

extern void lr_log(enum retro_log_level level, const char *fmt, ...);
extern void lr_message(const char *text, unsigned frames);

/* lr_presets.c: the machine presets and the core options */
extern void lr_presets_declare(retro_environment_t cb);
extern bool lr_presets_make_config(retro_environment_t cb, const char *content, char *cfg_path, size_t cfg_size);

/* Set to 0 by plat_power_off(): the guest powered the machine off */
extern volatile int cpu_thread_run;

extern char *ui_window_title(char *str);

/* lr_sound.c: one frame's worth of the mixed sources, as interleaved stereo int16 */
extern void lr_sound_init(void);
extern void lr_sound_reset(void);
extern unsigned lr_sound_mix(int16_t *out, unsigned frames, unsigned out_rate);

#ifdef __cplusplus
}
#endif

#endif
