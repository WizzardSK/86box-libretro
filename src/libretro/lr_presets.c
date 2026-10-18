/*
 * 86Box libretro core: machine presets, the core options that pick them, and the
 * 86box.cfg a preset becomes when the content is a disk or CD image rather than
 * a configuration file of its own.
 *
 * The generated configuration lives in <saves>/86box/<content name>/, so the
 * machine's NVRAM (CMOS settings) and a new hard disk stay with that content.
 */
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <86box/86box.h>
#include <86box/plat.h>

#include "libretro.h"
#include "lr_core.h"

typedef struct {
    const char *key;      /* core option value */
    const char *label;
    const char *machine;  /* internal names, as 86box.cfg takes them */
    const char *cpu_family;
    uint32_t    cpu_speed; /* Hz; 86Box takes the first CPU of the family at least this fast */
    uint32_t    mem_kb;
    const char *gfxcard;
    int         voodoo;
    const char *sndcard;  /* "none" for none */
    const char *mouse;
    const char *hdc;      /* "internal" for the board's own IDE; NULL for none */
    const char *fdd_a;    /* drive A: type */
    const char *fdd_b;    /* drive B: type, NULL for none */
    bool        cdrom;    /* an ATAPI CD-ROM drive on the secondary channel */
} preset_t;

/* One per era, each a board 86Box emulates well with its ROM in the v6.0 set,
 * and cards Windows of that time has drivers for */
static const preset_t presets[] = {
    { "ibmpc", "IBM PC 5150 (8088, CGA)",
      "ibmpc82", "8088", 4772728, 640, "cga", 0, "none", "none", NULL, "525_2dd", NULL, false },
    { "386dx40", "386DX-40 (DOS games)",
      "asus386", "am386dx", 40000000, 8192, "et4000ax", 0, "sbprov2", "msserial", "ide_isa", "35_2hd", "525_2hd", false },
    { "486dx2_66", "486DX2-66 (DOS, Windows 3.1)",
      "486sp3g", "i486dx2", 66666666, 16384, "s3_trio64_pci", 0, "sb16", "ps2", "internal", "35_2hd", "525_2hd", true },
    { "pentium133", "Pentium 133 (Windows 95)",
      "zappa", "pentium_p54c", 133333333, 32768, "s3_trio64_pci", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "pentiummmx233", "Pentium MMX 233 (Windows 95/98)",
      "p55t2p4", "pentium_p55c", 233333333, 65536, "virge_dx_pci", 1, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "pentium2_300", "Pentium II 300 (Windows 98)",
      "p2bls", "pentium2_klamath", 300000000, 131072, "voodoo3_3k_agp", 0, "es1371", "ps2", "internal", "35_2hd", NULL, true },
    { "pentium2_450", "Pentium II 450 (Windows 98, 2000)",
      "p3bf", "pentium2_deschutes", 450000000, 262144, "voodoo3_3k_agp", 0, "es1371", "ps2", "internal", "35_2hd", NULL, true },
};
#define NPRESETS (sizeof(presets) / sizeof(presets[0]))
#define DEFAULT_PRESET "pentium2_300"

static struct retro_core_option_v2_category option_cats[] = {
    { NULL, NULL, NULL },
};

static struct retro_core_option_v2_definition option_defs[] = {
    {
        "86box_machine", "Machine (Restart)", NULL,
        "The PC to build when the content is a disk, floppy or CD image. A 86box.cfg as content keeps its own machine.",
        NULL, NULL,
        { { NULL, NULL } }, /* filled from presets[] in lr_presets_declare */
        DEFAULT_PRESET
    },
    {
        "86box_new_hdd", "New Hard Disk (Restart)", NULL,
        "With a CD or floppy image as content, a blank hard disk of this size is made beside the machine's configuration (in saves/86box/<content>), for installing to. It is made once and kept.",
        NULL, NULL,
        {
            { "none", "None" },
            { "504", "504 MB" },
            { "2048", "2 GB" },
            { "8064", "8 GB" },
            { NULL, NULL },
        },
        "2048"
    },
    {
        "86box_dynarec", "Dynamic Recompiler (Restart)", NULL,
        "Translates the emulated CPU's code to the host's instead of interpreting it: much faster. Off for a program that misbehaves with it.",
        NULL, NULL,
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    { NULL, NULL, NULL, NULL, NULL, NULL, { { NULL, NULL } }, NULL },
};

static struct retro_core_options_v2 options = { option_cats, option_defs };

void
lr_presets_declare(retro_environment_t cb)
{
    for (size_t i = 0; i < NPRESETS && i < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1; i++) {
        option_defs[0].values[i].value = presets[i].key;
        option_defs[0].values[i].label = presets[i].label;
    }
    option_defs[0].values[NPRESETS].value = NULL;

    unsigned version = 0;
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2)
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options);
    else {
        /* A frontend without v2 options: the plain variables */
        static struct retro_variable vars[4];
        static char machine_desc[1024];
        size_t n = (size_t) snprintf(machine_desc, sizeof(machine_desc), "Machine (Restart); %s", DEFAULT_PRESET);
        for (size_t i = 0; i < NPRESETS; i++)
            if (strcmp(presets[i].key, DEFAULT_PRESET) && n < sizeof(machine_desc))
                n += (size_t) snprintf(machine_desc + n, sizeof(machine_desc) - n, "|%s", presets[i].key);
        vars[0] = (struct retro_variable) { "86box_machine", machine_desc };
        vars[1] = (struct retro_variable) { "86box_new_hdd", "New Hard Disk (Restart); 2048|none|504|8064" };
        vars[2] = (struct retro_variable) { "86box_dynarec", "Dynamic Recompiler (Restart); enabled|disabled" };
        vars[3] = (struct retro_variable) { NULL, NULL };
        cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
    }
}

static const char *
option(retro_environment_t cb, const char *key, const char *fallback)
{
    struct retro_variable var = { key, NULL };
    if (cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        return var.value;
    return fallback;
}

/* A raw disk of size bytes, 63 sectors and 16 heads; sparse where the
 * filesystem allows. False when it cannot be made. */
static bool
make_blank_disk(const char *path, long long size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
#ifdef _WIN32
    bool ok = _fseeki64(f, size - 1, SEEK_SET) == 0 && fputc(0, f) != EOF;
#else
    bool ok = fseeko(f, (off_t) (size - 1), SEEK_SET) == 0 && fputc(0, f) != EOF;
#endif
    fclose(f);
    return ok;
}

static void
hdd_line(FILE *f, int n, const char *path, long long bytes, const char *channel)
{
    long long cyl = bytes / (63LL * 16 * 512);
    if (cyl > 16383)
        cyl = 16383;
    fprintf(f, "hdd_%02i_parameters = 63, 16, %lld, 0, ide\n", n, cyl);
    fprintf(f, "hdd_%02i_fn = %s\n", n, path);
    fprintf(f, "hdd_%02i_ide_channel = %s\n", n, channel);
}

bool
lr_presets_make_config(retro_environment_t cb, const char *content, char *cfg_path, size_t cfg_size)
{
    if (lr_ext_is(lr_extension(content), "cfg")) {
        snprintf(cfg_path, cfg_size, "%s", content);
        return true;
    }

    const char *key    = option(cb, "86box_machine", DEFAULT_PRESET);
    const preset_t *pr = &presets[0];
    for (size_t i = 0; i < NPRESETS; i++)
        if (!strcmp(presets[i].key, key))
            pr = &presets[i];

    /* What the content holds (an image, or an .m3u's): the floppy and CD in
     * the drives at the start, and a hard disk */
    lr_disk_init(content);
    const char *floppy = lr_disk_inserted(LR_MEDIA_FLOPPY);
    const char *cd     = lr_disk_inserted(LR_MEDIA_CD);
    const char *hdd    = lr_disk_inserted(LR_MEDIA_HDD);

    /* <saves>/86box/<content name>/ */
    const char *save_dir = NULL;
    if (!cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) || !save_dir)
        save_dir = lr_data_dir();
    const char *base = strrchr(content, '/');
#ifdef _WIN32
    const char *bbase = strrchr(content, '\\');
    if (bbase > base)
        base = bbase;
#endif
    base = base ? base + 1 : content;
    char name[256];
    snprintf(name, sizeof(name), "%s", base);
    char *dot = strrchr(name, '.');
    if (dot)
        *dot = '\0';
    char dir[1200];
    snprintf(dir, sizeof(dir), "%s/86box", save_dir);
    plat_dir_create(dir);
    snprintf(dir, sizeof(dir), "%s/86box/%s", save_dir, name);
    plat_dir_create(dir);
    snprintf(cfg_path, cfg_size, "%s/86box.cfg", dir);

    FILE *f = fopen(cfg_path, "w");
    if (!f) {
        lr_log(RETRO_LOG_ERROR, "cannot write %s\n", cfg_path);
        return false;
    }
    const bool dynarec = strcmp(option(cb, "86box_dynarec", "enabled"), "disabled") != 0;
    fprintf(f, "# Made by the libretro core from its \"%s\" machine preset; rewritten at each start.\n"
               "# For a machine of your own, load a 86box.cfg as content instead.\n\n", pr->label);
    fprintf(f, "[Machine]\nmachine = %s\ncpu_family = %s\ncpu_speed = %u\nmem_size = %u\ncpu_use_dynarec = %d\ntime_sync = local\n\n",
            pr->machine, pr->cpu_family, pr->cpu_speed, pr->mem_kb, dynarec ? 1 : 0);
    fprintf(f, "[Video]\ngfxcard = %s\nvoodoo = %d\n\n", pr->gfxcard, pr->voodoo);
    fprintf(f, "[Input devices]\nmouse_type = %s\n\n", pr->mouse);
    fprintf(f, "[Sound]\nsndcard = %s\n\n", pr->sndcard);
    if (pr->hdc)
        fprintf(f, "[Storage controllers]\nhdc_1 = %s\n\n", pr->hdc);

    fprintf(f, "[Hard disks]\n");
    if (pr->hdc) {
        if (hdd)
            hdd_line(f, 1, hdd, lr_file_size(hdd), "0:0");
        else {
            const long long mb = atoll(option(cb, "86box_new_hdd", "2048"));
            if (mb > 0) {
                char disk[1300];
                snprintf(disk, sizeof(disk), "%s/hdd-%lldmb.img", dir, mb);
                const long long bytes = (mb * 1024 * 1024) / (63LL * 16 * 512) * (63LL * 16 * 512);
                if (lr_file_size(disk) < 0 && !make_blank_disk(disk, bytes))
                    lr_log(RETRO_LOG_ERROR, "cannot make the hard disk %s\n", disk);
                else
                    hdd_line(f, 1, disk, bytes, "0:0");
            }
        }
    }
    fprintf(f, "\n[Floppy and CD-ROM drives]\nfdd_01_type = %s\n", pr->fdd_a);
    if (floppy)
        fprintf(f, "fdd_01_fn = %s\n", floppy);
    fprintf(f, "fdd_02_type = %s\n", pr->fdd_b ? pr->fdd_b : "none");
    if (pr->cdrom) {
        fprintf(f, "cdrom_01_parameters = 1, atapi\ncdrom_01_ide_channel = 1:0\n");
        if (cd)
            fprintf(f, "cdrom_01_image_path = %s\n", cd);
    }
    fclose(f);

    lr_log(RETRO_LOG_INFO, "machine \"%s\" for %s (floppy %s, CD %s, hard disk %s), configuration %s\n", pr->label,
           content, floppy ? floppy : "none", cd ? cd : "none", hdd ? hdd : "none", cfg_path);
    if ((cd && !pr->cdrom) || (hdd && !pr->hdc)) {
        char text[300];
        snprintf(text, sizeof(text), "The %s machine has no %s drive for this content; pick another machine", pr->label,
                 cd && !pr->cdrom ? "CD-ROM" : "hard disk");
        lr_message(text, 600);
    }
    return true;
}
