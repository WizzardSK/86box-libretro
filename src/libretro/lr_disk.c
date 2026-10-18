/*
 * 86Box libretro core: the content's media, and RetroArch's Disc Control for
 * the machine's floppy drive A: and first CD-ROM drive.
 *
 * The content is one image, or an .m3u listing several. Each is a CD, a floppy
 * or a hard disk by its extension (and an .img by its size). The floppies and
 * CDs make Disc Control's list, with any appended from the menu; a hard disk is
 * attached once, by the machine's configuration.
 *
 * One image of the list is the current one. Ejecting empties the drive of its
 * kind; closing the tray puts the selected image into the drive of its kind, so
 * a floppy and a CD can be swapped independently. A CD change is signalled to
 * the machine as 86Box's own media menu does, so the guest sees a new disc.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/plat.h>

#include "libretro.h"
#include "lr_core.h"

#define LR_MAX_IMAGES 32

typedef struct {
    char       path[1024];
    lr_media_t kind;
} lr_image_t;

static lr_image_t images[LR_MAX_IMAGES];
static unsigned   num_images;
static unsigned   current;
static bool       ejected;
static char       hdd[1024];
static unsigned   initial_index;
static char       initial_path[1024];

const char *
lr_extension(const char *path)
{
    const char *dot   = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (bslash > slash)
        slash = bslash;
#endif
    return (dot && (!slash || dot > slash)) ? dot + 1 : "";
}

bool
lr_ext_is(const char *ext, const char *list)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", list);
    for (char *tok = strtok(buf, "|"); tok; tok = strtok(NULL, "|")) {
        size_t i = 0;
        while (tok[i] && ext[i] && tolower((unsigned char) tok[i]) == tolower((unsigned char) ext[i]))
            i++;
        if (!tok[i] && !ext[i])
            return true;
    }
    return false;
}

long long
lr_file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long long) st.st_size : -1;
}

lr_media_t
lr_media_kind(const char *path)
{
    const char *ext = lr_extension(path);
    if (lr_ext_is(ext, "iso|cue|chd|mds|ccd"))
        return LR_MEDIA_CD;
    if (lr_ext_is(ext, "ima|flp|vfd|dsk|86f|imd|td0|fdi|mfm|xdf"))
        return LR_MEDIA_FLOPPY;
    if (lr_ext_is(ext, "img")) {
        /* Up to a 2.88 MB floppy; a raw hard disk beyond */
        const long long size = lr_file_size(path);
        if (size > 0 && size <= 2949120)
            return LR_MEDIA_FLOPPY;
    }
    return LR_MEDIA_HDD;
}

static void
add(const char *path)
{
    if (!path || !*path)
        return;
    const lr_media_t kind = lr_media_kind(path);
    if (kind == LR_MEDIA_HDD) {
        if (!hdd[0])
            snprintf(hdd, sizeof(hdd), "%s", path);
        else
            lr_log(RETRO_LOG_WARN, "one hard disk is attached; %s is left out\n", path);
        return;
    }
    if (num_images >= LR_MAX_IMAGES)
        return;
    snprintf(images[num_images].path, sizeof(images[num_images].path), "%s", path);
    images[num_images].kind = kind;
    num_images++;
}

void
lr_disk_init(const char *content)
{
    num_images = 0;
    current    = 0;
    ejected    = false;
    hdd[0]     = '\0';

    if (!lr_ext_is(lr_extension(content), "m3u")) {
        add(content);
        return;
    }

    FILE *f = fopen(content, "r");
    if (!f) {
        lr_log(RETRO_LOG_ERROR, "cannot read %s\n", content);
        return;
    }
    /* Entries are relative to the .m3u's directory */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", content);
    char *end = dir + strlen(dir);
    while (end > dir && end[-1] != '/' && end[-1] != '\\')
        end--;
    *end = '\0';

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (isspace((unsigned char) *s))
            s++;
        char *e = s + strlen(s);
        while (e > s && isspace((unsigned char) e[-1]))
            *--e = '\0';
        if (!*s || *s == '#')
            continue;
        /* An optional "|label" after the path */
        char *bar = strchr(s, '|');
        if (bar)
            *bar = '\0';
        char path[2100];
        if (*s == '/' || *s == '\\' || (s[0] && s[1] == ':'))
            snprintf(path, sizeof(path), "%s", s);
        else
            snprintf(path, sizeof(path), "%s%s", dir, s);
        add(path);
    }
    fclose(f);

    /* The image a savestate or the last session left in, while the list is the same */
    if (initial_index < num_images && !strcmp(images[initial_index].path, initial_path))
        current = initial_index;
}

const char *
lr_disk_inserted(lr_media_t kind)
{
    if (kind == LR_MEDIA_HDD)
        return hdd[0] ? hdd : NULL;
    if (current < num_images && images[current].kind == kind)
        return images[current].path;
    for (unsigned i = 0; i < num_images; i++)
        if (images[i].kind == kind)
            return images[i].path;
    return NULL;
}

static void
insert(const lr_image_t *img)
{
    if (img->kind == LR_MEDIA_CD) {
        const int was_empty = cdrom_is_empty(0);
        cdrom_exit(0);
        memset(cdrom[0].image_path, 0, sizeof(cdrom[0].image_path));
        cdrom_load(&cdrom[0], (char *) img->path, 1);
        /* The media change, signalled as 86Box's media menu does */
        if (cdrom[0].insert) {
            cdrom[0].insert(cdrom[0].priv);
            /* From an empty drive straight to UNIT ATTENTION */
            if (was_empty)
                cdrom[0].insert(cdrom[0].priv);
        }
    } else
        floppy_mount(0, (char *) img->path, 0);
    lr_log(RETRO_LOG_INFO, "%s in %s\n", img->path, img->kind == LR_MEDIA_CD ? "the CD-ROM drive" : "drive A:");
}

static bool RETRO_CALLCONV
set_eject_state(bool eject)
{
    if (eject == ejected)
        return true;
    if (current < num_images && images[current].path[0]) {
        if (!eject)
            insert(&images[current]);
        else if (images[current].kind == LR_MEDIA_CD)
            cdrom_eject(0);
        else
            floppy_eject(0);
    }
    ejected = eject;
    return true;
}

static bool RETRO_CALLCONV
get_eject_state(void)
{
    return ejected;
}

static unsigned RETRO_CALLCONV
get_image_index(void)
{
    return current;
}

static bool RETRO_CALLCONV
set_image_index(unsigned index)
{
    /* num_images selects no disc */
    if (!ejected || index > num_images)
        return false;
    current = index;
    return true;
}

static unsigned RETRO_CALLCONV
get_num_images(void)
{
    return num_images;
}

static bool RETRO_CALLCONV
replace_image_index(unsigned index, const struct retro_game_info *info)
{
    if (index >= num_images)
        return false;
    if (!info) {
        memmove(&images[index], &images[index + 1], (num_images - index - 1) * sizeof(images[0]));
        num_images--;
        if (current > index)
            current--;
        return true;
    }
    if (!info->path || lr_media_kind(info->path) == LR_MEDIA_HDD) {
        lr_log(RETRO_LOG_WARN, "%s is not a floppy or CD image\n", info->path ? info->path : "(no path)");
        return false;
    }
    snprintf(images[index].path, sizeof(images[index].path), "%s", info->path);
    images[index].kind = lr_media_kind(info->path);
    return true;
}

static bool RETRO_CALLCONV
add_image_index(void)
{
    if (num_images >= LR_MAX_IMAGES)
        return false;
    images[num_images].path[0] = '\0';
    images[num_images].kind    = LR_MEDIA_FLOPPY;
    num_images++;
    return true;
}

static bool RETRO_CALLCONV
set_initial_image(unsigned index, const char *path)
{
    initial_index = index;
    snprintf(initial_path, sizeof(initial_path), "%s", path ? path : "");
    return true;
}

static bool RETRO_CALLCONV
get_image_path(unsigned index, char *path, size_t len)
{
    if (index >= num_images || !images[index].path[0])
        return false;
    snprintf(path, len, "%s", images[index].path);
    return true;
}

static bool RETRO_CALLCONV
get_image_label(unsigned index, char *label, size_t len)
{
    if (index >= num_images || !images[index].path[0])
        return false;
    const char *p    = images[index].path;
    const char *base = p + strlen(p);
    while (base > p && base[-1] != '/' && base[-1] != '\\')
        base--;
    snprintf(label, len, "%s (%s)", base, images[index].kind == LR_MEDIA_CD ? "CD" : "floppy");
    return true;
}

void
lr_disk_declare(retro_environment_t cb)
{
    static struct retro_disk_control_ext_callback ext = {
        set_eject_state, get_eject_state, get_image_index, set_image_index, get_num_images,
        replace_image_index, add_image_index, set_initial_image, get_image_path, get_image_label
    };
    static struct retro_disk_control_callback basic = {
        set_eject_state, get_eject_state, get_image_index, set_image_index, get_num_images,
        replace_image_index, add_image_index
    };
    unsigned version = 0;
    if (cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &version) && version >= 1)
        cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &ext);
    else
        cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &basic);
}
