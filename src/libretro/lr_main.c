/*
 * 86Box libretro core: the libretro API, in place of the Qt or SDL frontend.
 *
 * The content is a machine's configuration file (86box.cfg); its folder is the
 * machine's folder, as with "-C" on the command line, and its disks are where
 * that file says. The ROM set goes into <system>/86box/roms.
 *
 * pc_run() emulates one millisecond, so retro_run() runs it for the length of
 * a frame at the rate the core reports - 16 or 17 times at 60 Hz - on the
 * frontend's thread, with no emulation thread of its own. The video card's
 * blits are copied into a frame of the core's, which retro_run() hands over;
 * the sound sources are mixed into one frame's worth of audio (lr_sound.c).
 */
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include <86box/version.h>
#include <86box/video.h>

#include "libretro.h"
#include "lr_core.h"

#define FRAME_RATE   60.0
#define SAMPLE_RATE  48000
#define MAX_WIDTH    2048
#define MAX_HEIGHT   2048

static retro_environment_t        environ_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;
static retro_log_printf_t         log_cb;

static char data_dir[1024];
static bool game_loaded = false;
/* The content and the configuration it was started from (a preset's, or a
 * 86box.cfg loaded as content), for rebuilding the machine at Reset */
static char content_path[1400];
static char lr_cfg_path[1400];

/* The frame: what the video card blitted last, at its size */
static uint32_t       *frame;
static int             frame_w = 640;
static int             frame_h = 480;
static int             geometry_w;
static int             geometry_h;
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;

/* Emulated milliseconds still owed to the frames handed over */
static double ms_owed;
static int    ms_since_onesec;

/* What the SDL frontend defines for the emulator */
int          rctrl_is_lalt;
int          update_icons;
int          kbd_req_capture;
int          hide_status_bar;
int          hide_tool_bar;
bool         fast_forward = false;
int          fixed_size_x = 640;
int          fixed_size_y = 480;
int          mouse_capture = 1;
volatile int cpu_thread_run = 1;
volatile int lr_powered_off;

/* The user shader lists config.c reads and writes; the core draws no shaders */
char gl3_shader_file[20][512];
char vk_shader_file[20][512];

static pthread_mutex_t blit_lock = PTHREAD_MUTEX_INITIALIZER;
static char            window_title[512] = EMU_NAME;

void
lr_log(enum retro_log_level level, const char *fmt, ...)
{
    char    buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (log_cb)
        log_cb(level, "[86Box] %s", buf);
    else
        fprintf(stderr, "[86Box] %s", buf);
}

void
lr_message(const char *text, unsigned frames)
{
    struct retro_message msg = { text, frames };
    if (environ_cb)
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

const char *
lr_data_dir(void)
{
    return data_dir;
}

/* ---------------------------------------------------------------------------
 * What the frontend provides to the emulator: blits, the UI, the window
 * ------------------------------------------------------------------------- */

void
startblit(void)
{
    pthread_mutex_lock(&blit_lock);
}

void
endblit(void)
{
    pthread_mutex_unlock(&blit_lock);
}

static void
lr_blit(int x, int y, int w, int h, int monitor_index)
{
    if ((monitor_index == 0) && (buffer32 != NULL) && (x >= 0) && (y >= 0) && (w > 0) && (h > 0) &&
        (w <= MAX_WIDTH) && (h <= MAX_HEIGHT)) {
        pthread_mutex_lock(&frame_lock);
        for (int row = 0; row < h; row++)
            memcpy(&frame[row * MAX_WIDTH], &(buffer32->line[y + row][x]), w * sizeof(uint32_t));
        frame_w = w;
        frame_h = h;
        pthread_mutex_unlock(&frame_lock);
    }
    video_blit_complete_monitor(monitor_index);
}

/* A fatal error (pc_fatal): the machine stops; retro_run keeps showing its last frame */
void
do_stop(void)
{
    cpu_thread_run = 0;
}

void
plat_resize(UNUSED(int w), UNUSED(int h), UNUSED(int monitor_index))
{
    /* The frame is handed over at the size of each blit */
}

void
plat_resize_request(UNUSED(int w), UNUSED(int h), int monitor_index)
{
    atomic_store((&doresize_monitors[monitor_index]), 1);
}

void
plat_mouse_capture(int on)
{
    /* The frontend owns the pointer; the guest always gets its motion. The
     * mice pass on nothing while this is 0, and 86Box only ever turns it off */
    (void) on;
    mouse_capture = 1;
}

int
plat_vidapi(UNUSED(const char *api))
{
    return 0;
}

char *
ui_window_title(char *str)
{
    if (!str)
        return window_title;
    strncpy(window_title, str, sizeof(window_title) - 1);
    return str;
}

void
ui_emu_status(UNUSED(int speed_percent))
{
}

void
ui_init_monitor(UNUSED(int monitor_index))
{
}

void
ui_deinit_monitor(UNUSED(int monitor_index))
{
}

int
ui_msgbox(int flags, char *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

/* No dialogs in a core: the message goes to the log and on screen, and a
   question is answered "yes", what the user most likely picks to go on */
int
ui_msgbox_header(int flags, char *header, char *message)
{
    char text[1024];

    snprintf(text, sizeof(text), "%s%s%s", header ? header : "", header ? ": " : "", message ? message : "");
    lr_log((flags & (MBX_ERROR | MBX_FATAL)) ? RETRO_LOG_ERROR : RETRO_LOG_INFO, "%s\n", text);
    lr_message(text, 600);
    return (flags & MBX_QUESTION_YN) ? 1 : 0;
}

void
ui_progress_wait(UNUSED(const char *message), int total, int (*poll)(void *arg), void *arg)
{
    while (poll(arg) < total)
        plat_delay_ms(50);
}

int
ui_confirm_unsupported_hardware(const ui_unsupported_hardware_t *items, int count, UNUSED(int machine_missing))
{
    for (int i = 0; i < count; i++)
        lr_log(RETRO_LOG_WARN, "unsupported in this build: %s\n", items[i].name);
    return 1;
}

void ui_sb_update_icon_state(UNUSED(int tag), UNUSED(int state)) { }
void ui_sb_update_icon(UNUSED(int tag), UNUSED(int active)) { }
void ui_sb_update_icon_write(UNUSED(int tag), UNUSED(int active)) { }
void ui_sb_update_icon_wp(UNUSED(int tag), UNUSED(int state)) { }
void ui_sb_update_tip(UNUSED(int arg)) { }
void ui_sb_update_panes(void) { }
void ui_sb_update_text(void) { }
void ui_sb_set_text(UNUSED(char *wstr)) { }
void ui_sb_bugui(UNUSED(char *str)) { }
void ui_sb_set_ready(UNUSED(int ready)) { }
void ui_sb_mt32lcd(UNUSED(char *str)) { }
void ui_hard_reset_completed(void) { }
void ui_update_force_interpreter(void) { }

/* ---------------------------------------------------------------------------
 * Joystick: RetroPads 1-4 as the host joysticks 86Box maps to its gameport
 * ------------------------------------------------------------------------- */

int                   joysticks_present = 0;
joystick_state_t      joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
plat_joystick_state_t plat_joystick_state[MAX_PLAT_JOYSTICKS];

#define LR_JOYSTICKS 4

static const unsigned pad_buttons[] = {
    RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X,
    RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
    RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3
};
#define PAD_BUTTONS ((int) (sizeof(pad_buttons) / sizeof(pad_buttons[0])))

void
joystick_init(void)
{
    joysticks_present = LR_JOYSTICKS;
    for (int js = 0; js < LR_JOYSTICKS; js++) {
        plat_joystick_state_t *s = &plat_joystick_state[js];
        snprintf(s->name, sizeof(s->name), "RetroPad %i", js + 1);
        s->nr_axes    = MIN(4, MAX_JOY_AXES);
        s->nr_buttons = MIN(PAD_BUTTONS, MAX_JOY_BUTTONS);
        s->nr_povs    = MIN(1, MAX_JOY_POVS);
        for (int a = 0; a < s->nr_axes; a++) {
            snprintf(s->axis[a].name, sizeof(s->axis[a].name), "Axis %i", a);
            s->axis[a].id = a;
        }
        for (int b = 0; b < s->nr_buttons; b++) {
            snprintf(s->button[b].name, sizeof(s->button[b].name), "Button %i", b);
            s->button[b].id = b;
        }
        for (int p = 0; p < s->nr_povs; p++) {
            snprintf(s->pov[p].name, sizeof(s->pov[p].name), "POV %i", p);
            s->pov[p].id = p;
        }
    }
}

void
joystick_close(void)
{
}

/* The D-pad as a hat, in degrees, -1 when centred */
static int
pad_hat(unsigned port)
{
    const int up    = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    const int down  = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    const int left  = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    const int right = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    const int x     = right - left;
    const int y     = down - up;

    if (!x && !y)
        return -1;
    return ((int) (atan2((double) y, (double) x) * 180.0 / M_PI) + 90 + 360) % 360;
}

static int
joystick_get_axis(int joystick_nr, int mapping)
{
    const int hat = plat_joystick_state[joystick_nr].p[mapping & 3];

    if (mapping & POV_X)
        return (hat < 0) ? 0 : (int) (sin(hat * M_PI / 180.0) * 32767.0);
    if (mapping & POV_Y)
        return (hat < 0) ? 0 : (int) (-cos(hat * M_PI / 180.0) * 32767.0);
    return plat_joystick_state[joystick_nr].a[plat_joystick_state[joystick_nr].axis[mapping].id];
}

void
joystick_process(uint8_t gp)
{
    if (!joystick_type[gp] || !input_state_cb)
        return;

    for (int js = 0; js < LR_JOYSTICKS; js++) {
        plat_joystick_state_t *s = &plat_joystick_state[js];
        s->a[0] = input_state_cb(js, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        s->a[1] = input_state_cb(js, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        s->a[2] = input_state_cb(js, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        s->a[3] = input_state_cb(js, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
        for (int b = 0; b < s->nr_buttons; b++)
            s->b[b] = input_state_cb(js, RETRO_DEVICE_JOYPAD, 0, pad_buttons[b]) ? 128 : 0;
        s->p[0] = pad_hat(js);
    }

    for (int js = 0; js < joystick_get_max_joysticks(joystick_type[gp]); js++) {
        if (joystick_state[gp][js].plat_joystick_nr) {
            const int nr = joystick_state[gp][js].plat_joystick_nr - 1;

            for (int a = 0; a < joystick_get_axis_count(joystick_type[gp]); a++)
                joystick_state[gp][js].axis[a] = joystick_get_axis(nr, joystick_state[gp][js].axis_mapping[a]);
            for (int b = 0; b < joystick_get_button_count(joystick_type[gp]); b++)
                joystick_state[gp][js].button[b] = plat_joystick_state[nr].b[joystick_state[gp][js].button_mapping[b]];
            for (int p = 0; p < joystick_get_pov_count(joystick_type[gp]); p++)
                joystick_state[gp][js].pov[p] = plat_joystick_state[nr].p[0];
        } else {
            for (int a = 0; a < joystick_get_axis_count(joystick_type[gp]); a++)
                joystick_state[gp][js].axis[a] = 0;
            for (int b = 0; b < joystick_get_button_count(joystick_type[gp]); b++)
                joystick_state[gp][js].button[b] = 0;
            for (int p = 0; p < joystick_get_pov_count(joystick_type[gp]); p++)
                joystick_state[gp][js].pov[p] = -1;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Keyboard: the frontend's keys as XT scan codes (0x1xx: the E0 prefix)
 * ------------------------------------------------------------------------- */

static uint16_t
retrok_to_xt(unsigned key)
{
    switch (key) {
        case RETROK_ESCAPE: return 0x01;
        case RETROK_1: return 0x02;
        case RETROK_2: return 0x03;
        case RETROK_3: return 0x04;
        case RETROK_4: return 0x05;
        case RETROK_5: return 0x06;
        case RETROK_6: return 0x07;
        case RETROK_7: return 0x08;
        case RETROK_8: return 0x09;
        case RETROK_9: return 0x0A;
        case RETROK_0: return 0x0B;
        case RETROK_MINUS: return 0x0C;
        case RETROK_EQUALS: return 0x0D;
        case RETROK_BACKSPACE: return 0x0E;
        case RETROK_TAB: return 0x0F;
        case RETROK_q: return 0x10;
        case RETROK_w: return 0x11;
        case RETROK_e: return 0x12;
        case RETROK_r: return 0x13;
        case RETROK_t: return 0x14;
        case RETROK_y: return 0x15;
        case RETROK_u: return 0x16;
        case RETROK_i: return 0x17;
        case RETROK_o: return 0x18;
        case RETROK_p: return 0x19;
        case RETROK_LEFTBRACKET: return 0x1A;
        case RETROK_RIGHTBRACKET: return 0x1B;
        case RETROK_RETURN: return 0x1C;
        case RETROK_LCTRL: return 0x1D;
        case RETROK_a: return 0x1E;
        case RETROK_s: return 0x1F;
        case RETROK_d: return 0x20;
        case RETROK_f: return 0x21;
        case RETROK_g: return 0x22;
        case RETROK_h: return 0x23;
        case RETROK_j: return 0x24;
        case RETROK_k: return 0x25;
        case RETROK_l: return 0x26;
        case RETROK_SEMICOLON: return 0x27;
        case RETROK_QUOTE: return 0x28;
        case RETROK_BACKQUOTE: return 0x29;
        case RETROK_LSHIFT: return 0x2A;
        case RETROK_BACKSLASH: return 0x2B;
        case RETROK_z: return 0x2C;
        case RETROK_x: return 0x2D;
        case RETROK_c: return 0x2E;
        case RETROK_v: return 0x2F;
        case RETROK_b: return 0x30;
        case RETROK_n: return 0x31;
        case RETROK_m: return 0x32;
        case RETROK_COMMA: return 0x33;
        case RETROK_PERIOD: return 0x34;
        case RETROK_SLASH: return 0x35;
        case RETROK_RSHIFT: return 0x36;
        case RETROK_KP_MULTIPLY: return 0x37;
        case RETROK_LALT: return 0x38;
        case RETROK_SPACE: return 0x39;
        case RETROK_CAPSLOCK: return 0x3A;
        case RETROK_F1: return 0x3B;
        case RETROK_F2: return 0x3C;
        case RETROK_F3: return 0x3D;
        case RETROK_F4: return 0x3E;
        case RETROK_F5: return 0x3F;
        case RETROK_F6: return 0x40;
        case RETROK_F7: return 0x41;
        case RETROK_F8: return 0x42;
        case RETROK_F9: return 0x43;
        case RETROK_F10: return 0x44;
        case RETROK_NUMLOCK: return 0x45;
        case RETROK_SCROLLOCK: return 0x46;
        case RETROK_KP7: return 0x47;
        case RETROK_KP8: return 0x48;
        case RETROK_KP9: return 0x49;
        case RETROK_KP_MINUS: return 0x4A;
        case RETROK_KP4: return 0x4B;
        case RETROK_KP5: return 0x4C;
        case RETROK_KP6: return 0x4D;
        case RETROK_KP_PLUS: return 0x4E;
        case RETROK_KP1: return 0x4F;
        case RETROK_KP2: return 0x50;
        case RETROK_KP3: return 0x51;
        case RETROK_KP0: return 0x52;
        case RETROK_KP_PERIOD: return 0x53;
        case RETROK_LESS: return 0x56;
        case RETROK_F11: return 0x57;
        case RETROK_F12: return 0x58;
        case RETROK_KP_ENTER: return 0x11C;
        case RETROK_RCTRL: return 0x11D;
        case RETROK_KP_DIVIDE: return 0x135;
        case RETROK_PRINT: return 0x137;
        case RETROK_RALT: return 0x138;
        case RETROK_HOME: return 0x147;
        case RETROK_UP: return 0x148;
        case RETROK_PAGEUP: return 0x149;
        case RETROK_LEFT: return 0x14B;
        case RETROK_RIGHT: return 0x14D;
        case RETROK_END: return 0x14F;
        case RETROK_DOWN: return 0x150;
        case RETROK_PAGEDOWN: return 0x151;
        case RETROK_INSERT: return 0x152;
        case RETROK_DELETE: return 0x153;
        case RETROK_LSUPER: return 0x15B;
        case RETROK_RSUPER: return 0x15C;
        case RETROK_MENU: return 0x15D;
        default: return 0;
    }
}

static void RETRO_CALLCONV
lr_keyboard_event(bool down, unsigned keycode, UNUSED(uint32_t character), UNUSED(uint16_t key_modifiers))
{
    const uint16_t xt = retrok_to_xt(keycode);
    if (xt && game_loaded)
        keyboard_input(down, xt);
}

/* ---------------------------------------------------------------------------
 * The libretro API
 * ------------------------------------------------------------------------- */

RETRO_API void
retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    struct retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;

    lr_presets_declare(cb);
    lr_disk_declare(cb);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(UNUSED(retro_audio_sample_t cb)) { }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API unsigned
retro_api_version(void)
{
    return RETRO_API_VERSION;
}

RETRO_API void
retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = "86Box";
    info->library_version  = EMU_VERSION_FULL;
    info->valid_extensions = "cfg|m3u|iso|cue|chd|mds|ccd|img|ima|flp|vfd|dsk|86f|imd|td0|fdi|mfm|xdf|vhd|hdi|hdx|hdd";
    info->need_fullpath    = true;
    info->block_extract    = true;
}

RETRO_API void
retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = geometry_w ? geometry_w : 640;
    info->geometry.base_height  = geometry_h ? geometry_h : 480;
    info->geometry.max_width    = MAX_WIDTH;
    info->geometry.max_height   = MAX_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = FRAME_RATE;
    info->timing.sample_rate    = SAMPLE_RATE;
}

RETRO_API void
retro_init(void)
{
    const char *system_dir = NULL;

    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
        snprintf(data_dir, sizeof(data_dir), "%s/86box/", system_dir);
    else
        snprintf(data_dir, sizeof(data_dir), "86box/");

    frame = (uint32_t *) calloc(MAX_WIDTH * MAX_HEIGHT, sizeof(uint32_t));
    lr_sound_init();
}

RETRO_API void
retro_deinit(void)
{
    free(frame);
    frame = NULL;
}

RETRO_API void
retro_set_controller_port_device(UNUSED(unsigned port), UNUSED(unsigned device))
{
}

RETRO_API bool
retro_load_game(const struct retro_game_info *game)
{
    if (!game || !game->path) {
        lr_log(RETRO_LOG_ERROR, "content is needed: a machine's 86box.cfg, or a disk, floppy or CD image\n");
        return false;
    }

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
        return false;

    struct retro_keyboard_callback keyboard = { lr_keyboard_event };
    environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard);

    plat_dir_create(data_dir);

    /* A 86box.cfg as it is; an image gets the machine preset's configuration */
    snprintf(content_path, sizeof(content_path), "%s", game->path);
    lr_presets_follow_boards(environ_cb);   /* takes in the boards' values */
    if (!lr_presets_make_config(environ_cb, game->path, lr_cfg_path, sizeof(lr_cfg_path), false))
        return false;

    char *argv[] = { (char *) "86Box", (char *) "-C", lr_cfg_path, NULL };
    if (!pc_init(3, argv)) {
        lr_log(RETRO_LOG_ERROR, "pc_init failed for %s\n", cfg_path);
        return false;
    }
    if (!pc_init_roms()) {
        char text[1200];
        snprintf(text, sizeof(text), "No ROMs found: put the 86Box ROM set into %sroms", data_dir);
        lr_log(RETRO_LOG_ERROR, "%s\n", text);
        lr_message(text, 900);
        return false;
    }
    pc_init_modules();

    /* The high-precision timer is plat_timer_read(): nanoseconds */
    timer_freq = 1000000000ULL;

    video_setblit(lr_blit);
    pc_reset_hard_init();
    plat_pause(0);

    ms_owed         = 0.0;
    ms_since_onesec = 0;
    cpu_thread_run  = 1;
    lr_powered_off  = 0;
    game_loaded     = true;
    lr_log(RETRO_LOG_INFO, "%s, machine started from %s\n", EMU_NAME " " EMU_VERSION_FULL, game->path);
    return true;
}

RETRO_API bool
retro_load_game_special(UNUSED(unsigned type), UNUSED(const struct retro_game_info *info), UNUSED(size_t num))
{
    return false;
}

RETRO_API void
retro_unload_game(void)
{
    if (!game_loaded)
        return;
    game_loaded = false;
    pc_close(NULL);
}

RETRO_API void
retro_reset(void)
{
    if (!game_loaded)
        return;
    /* Machine or Hardware options changed: the machine is built again from a
     * new configuration, as standalone 86Box does when its settings change.
     * The floppy and CD in the drives stay. */
    lr_presets_follow_boards(environ_cb);
    if (lr_presets_changed(environ_cb)
        && lr_presets_make_config(environ_cb, content_path, lr_cfg_path, sizeof(lr_cfg_path), true)) {
        pc_reset_hard_close();
        config_load();
        pc_reset_hard_init();
        lr_log(RETRO_LOG_INFO, "machine rebuilt from %s after an option change\n", lr_cfg_path);
        return;
    }
    pc_reset_hard();
}

static void
poll_mouse(void)
{
    static int buttons_prev;

    const int dx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    const int dy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    if (dx || dy)
        mouse_scale(dx, dy);

    int buttons = 0;
    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
        buttons |= 1;
    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
        buttons |= 2;
    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
        buttons |= 4;
    if (buttons != buttons_prev) {
        mouse_set_buttons_ex(buttons);
        buttons_prev = buttons;
    }

    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP))
        mouse_set_z(1);
    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
        mouse_set_z(-1);
}

RETRO_API void
retro_run(void)
{
    static int16_t audio[2 * (SAMPLE_RATE / 50)];

    input_poll_cb();
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        lr_presets_follow_boards(environ_cb);
    /* The guest switched the machine off (Windows' shut down): close the
     * content, once, rather than keep showing its last frame */
    if (game_loaded && lr_powered_off == 1) {
        lr_powered_off = 2;
        lr_log(RETRO_LOG_INFO, "the machine was powered off\n");
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
    if (game_loaded && cpu_thread_run) {
        poll_mouse();

        /* The emulated time of this frame, a millisecond per pc_run() */
        ms_owed += 1000.0 / FRAME_RATE;
        while (ms_owed >= 1.0) {
            ms_owed -= 1.0;
            pc_run();
            if (++ms_since_onesec >= 1000) {
                ms_since_onesec = 0;
                pc_onesec();
            }
        }
    }

    pthread_mutex_lock(&frame_lock);
    const int w = frame_w;
    const int h = frame_h;
    if ((w != geometry_w) || (h != geometry_h)) {
        geometry_w = w;
        geometry_h = h;
        struct retro_game_geometry geometry = { (unsigned) w, (unsigned) h, MAX_WIDTH, MAX_HEIGHT, 4.0f / 3.0f };
        environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
    }
    video_cb(frame, w, h, MAX_WIDTH * sizeof(uint32_t));
    pthread_mutex_unlock(&frame_lock);

    const unsigned frames = (unsigned) (SAMPLE_RATE / FRAME_RATE);
    lr_sound_mix(audio, frames, SAMPLE_RATE);
    audio_batch_cb(audio, frames);
}

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(UNUSED(void *data), UNUSED(size_t size)) { return false; }
RETRO_API bool retro_unserialize(UNUSED(const void *data), UNUSED(size_t size)) { return false; }
RETRO_API void retro_cheat_reset(void) { }
RETRO_API void retro_cheat_set(UNUSED(unsigned index), UNUSED(bool enabled), UNUSED(const char *code)) { }
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API void *retro_get_memory_data(UNUSED(unsigned id)) { return NULL; }
RETRO_API size_t retro_get_memory_size(UNUSED(unsigned id)) { return 0; }
