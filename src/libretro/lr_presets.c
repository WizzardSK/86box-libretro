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
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/sound.h>
#include <86box/video.h>

#include "cpu.h"
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

/* Boards 86Box emulates well with their ROMs in the v6.0 set, from each era and
 * some well-known brand-name PCs, with cards Windows of that time has drivers for */
static const preset_t presets[] = {
    { "ibmpc", "IBM PC 5150 (8088, CGA)",
      "ibmpc82", "8088", 4772728, 640, "cga", 0, "none", "none", NULL, "525_2dd", NULL, false },
    { "ibmxt", "IBM PC/XT 5160 (8088, CGA, AdLib, hard disk)",
      "ibmxt86", "8088", 4772728, 640, "cga", 0, "adlib", "none", "xtide", "525_2dd", NULL, false },
    { "pcjr", "IBM PCjr (8088, its own graphics and sound)",
      "ibmpcjr", "8088", 4772728, 640, "internal", 0, "none", "none", NULL, "525_2dd", NULL, false },
    { "compaqportable", "Compaq Portable (8088, CGA, hard disk)",
      "portable", "8088", 4772728, 640, "compaq_cga", 0, "none", "none", "xtide", "525_2dd", "525_2dd", false },
    { "tandy1000sx", "Tandy 1000 SX (8088, Tandy graphics and sound, hard disk)",
      "tandy1000sx", "8088", 7159092, 640, "internal", 0, "none", "none", "xtide", "525_2dd", "525_2dd", false },
    { "europc", "Schneider EuroPC (8088, CGA, 3.5\" floppy)",
      "europc", "8088_europc", 9545456, 640, "cga", 0, "none", "msserial", NULL, "35_2dd", NULL, false },
    { "pc1512", "Amstrad PC1512 (8086, CGA, mouse, hard disk)",
      "pc1512", "8086", 8000000, 640, "internal", 0, "none", "internal", "xtide", "525_2dd", NULL, false },
    { "ibmat", "IBM PC/AT 5170 (286, EGA, AdLib, hard disk)",
      "ibmat", "286", 8000000, 512, "ega", 0, "adlib", "none", "ide_isa", "525_2hd", "525_2dd", false },
    { "286", "286-16 (DOS, VGA, Sound Blaster Pro)",
      "ami286", "286", 16000000, 2048, "et4000ax", 0, "sbprov2", "msserial", "ide_isa", "525_2hd", "35_2hd", false },
    { "t3100e", "Toshiba T3100e (286 laptop, plasma display)",
      "t3100e", "286", 12000000, 1024, "internal", 0, "none", "msserial", "internal", "35_2hd", NULL, false },
    { "ps1_2121", "IBM PS/1 2121 (386SX, Windows 3.1)",
      "ibmps1_2121", "i386sx", 16000000, 6144, "internal", 0, "internal", "ps2", "internal", "35_2hd", NULL, false },
    { "megapc", "Amstrad MegaPC (386SX-25, VGA, Sound Blaster Pro)",
      "megapc", "i386sx", 25000000, 4096, "internal", 0, "sbprov2", "ps2", "internal", "35_2hd", NULL, false },
    { "386dx40", "386DX-40 (DOS games)",
      "asus386", "am386dx", 40000000, 8192, "et4000ax", 0, "sbprov2", "msserial", "ide_isa", "35_2hd", "525_2hd", false },
    { "486sx25", "486SX-25 VL-Bus (DOS games, Windows 3.1)",
      "vli486sv2g", "i486sx", 25000000, 8192, "cl_gd5429_vlb", 0, "sb16", "ps2", "ide_vlb_2ch", "35_2hd", "525_2hd", true },
    { "valuepoint", "IBM PS/ValuePoint 433DX/Si (486DX-33, Windows 3.1)",
      "valuepoint433", "i486dx", 33333333, 16384, "internal", 0, "sb16", "ps2", "internal", "35_2hd", NULL, false },
    { "486dx2_66", "486DX2-66 (DOS, Windows 3.1)",
      "486sp3g", "i486dx2", 66666666, 16384, "s3_trio64_pci", 0, "sb16", "ps2", "internal", "35_2hd", "525_2hd", true },
    { "am5x86_133", "AMD 5x86-133 (DOS, Windows 95)",
      "486vipio2", "am5x86", 133333333, 32768, "s3_trio64_pci", 0, "sb16", "ps2", "internal", "35_2hd", "525_2hd", true },
    { "pb520r", "Packard Bell PB520R (Pentium 60)",
      "pb520r", "pentium_p5", 60000000, 16384, "internal", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "ibmpc330", "IBM PC 330 (Pentium 100, Windows 95)",
      "pc330_65x6", "pentium_p54c", 100000000, 32768, "internal", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "pentium133", "Pentium 133 (Windows 95)",
      "zappa", "pentium_p54c", 133333333, 32768, "s3_trio64_pci", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "presario4500", "Compaq Presario 4500 (Pentium 166, Windows 95)",
      "presario4500", "pentium_p54c", 166666666, 32768, "internal", 0, "internal", "ps2", "internal", "35_2hd", NULL, true },
    { "vaio", "Sony VAIO PCV-90 (Pentium 166, Windows 95)",
      "pcv90", "pentium_p54c", 166666666, 32768, "s3_trio64v2dx_pci", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "gateway2000", "Gateway 2000 (Pentium 200, Windows 95)",
      "gw2kte", "pentium_p54c", 200000000, 65536, "internal", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "dellxps", "Dell Dimension XPS (Pentium MMX 200, Voodoo, Windows 95)",
      "dellhannibalp", "pentium_p55c", 200000000, 65536, "virge_dx_pci", 1, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "pentiummmx233", "Pentium MMX 233 (Windows 95/98)",
      "p55t2p4", "pentium_p55c", 233333333, 65536, "virge_dx_pci", 1, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "6x86mx", "Cyrix 6x86MX (Windows 95/98)",
      "tx97", "cx6x86mx", 233333333, 65536, "virge_dx_pci", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "pentiumpro200", "Pentium Pro 200 (Windows NT 4.0)",
      "ap440fx", "pentiumpro", 200000000, 65536, "internal", 0, "internal", "ps2", "internal", "35_2hd", NULL, true },
    { "k6_2_400", "AMD K6-2 400 (Windows 98 games)",
      "p5a", "k6_2", 400000000, 131072, "voodoo3_3k_agp", 0, "es1371", "ps2", "internal", "35_2hd", NULL, true },
    { "k6_3_450", "AMD K6-III 450 (Windows 98)",
      "p5a", "k6_3", 450000000, 131072, "voodoo3_3k_agp", 0, "es1371", "ps2", "internal", "35_2hd", NULL, true },
    { "pentium2_300", "Pentium II 300 (Windows 98)",
      "p2bls", "pentium2_klamath", 300000000, 131072, "voodoo3_3k_agp", 0, "es1371", "ps2", "internal", "35_2hd", NULL, true },
    { "virtualpc", "Microsoft Virtual PC 2007 hardware (Pentium II, S3 Trio64, SB16)",
      "vpc2007", "pentium2_deschutes", 333333333, 262144, "s3_trio64_pci", 0, "sb16", "ps2", "internal", "35_2hd", NULL, true },
    { "pentium2_450", "Pentium II 450 (Windows 98, 2000)",
      "p3bf", "pentium2_deschutes", 450000000, 262144, "voodoo3_3k_agp", 0, "es1371", "ps2", "internal", "35_2hd", NULL, true },
    { "celeron533", "Celeron 533 (Windows 98, 2000)",
      "cubx", "celeron_mendocino", 533333333, 262144, "voodoo3_3k_agp", 0, "es1371", "ps2", "internal", "35_2hd", NULL, true },
};
#define NPRESETS (sizeof(presets) / sizeof(presets[0]))
#define DEFAULT_PRESET "pentium2_300"

/* The machine table's types, grouped into the lists of the All Machines
 * submenu: every machine 86Box emulates, the parts picked by its bus. The
 * Machine option's value for a group is "all_<key>", its list 86box_all_<key>. */
typedef struct {
    const char *key;
    const char *label;
    int         first_type, last_type; /* MACHINE_TYPE_ range */
    uint32_t    mem_kb;                /* within what the board takes */
} group_t;

static const group_t groups[] = {
    { "8088", "8088 / 8086", MACHINE_TYPE_8088, MACHINE_TYPE_8086, 640 },
    { "286", "286", MACHINE_TYPE_286, MACHINE_TYPE_286, 2048 },
    { "386sx", "386SX", MACHINE_TYPE_386SX, MACHINE_TYPE_486SLC, 4096 },
    { "386dx", "386DX", MACHINE_TYPE_386DX, MACHINE_TYPE_386DX_486, 8192 },
    { "486", "486", MACHINE_TYPE_SOCKET1, MACHINE_TYPE_SOCKET3, 16384 },
    { "486pci", "486 PCI", MACHINE_TYPE_SOCKET3_PCI, MACHINE_TYPE_STPC, 16384 },
    { "socket45", "Pentium (Socket 4, 5)", MACHINE_TYPE_SOCKET4, MACHINE_TYPE_SOCKET5, 32768 },
    { "socket7", "Pentium MMX, K6 (Socket 7)", MACHINE_TYPE_SOCKET7_3V, MACHINE_TYPE_SOCKET7, 65536 },
    { "ss7", "K6-2, K6-III (Super Socket 7)", MACHINE_TYPE_SOCKETS7, MACHINE_TYPE_SOCKETS7, 131072 },
    { "p6", "Pentium Pro, II, Celeron (Socket 8, Slot 1, Socket 370)", MACHINE_TYPE_SOCKET8, MACHINE_TYPE_MISC, 131072 },
};
#define NGROUPS (sizeof(groups) / sizeof(groups[0]))

static struct retro_core_option_v2_category option_cats[] = {
    { "machines", "All Machines (Reset)", "Every machine 86Box emulates, by CPU. Set Machine to one of the \"any machine\" entries to use the list of that group; the machine needs its ROM in the ROM set." },
    { "hardware", "Hardware (Reset)", "Change parts of the machine preset. Only for a disk, floppy or CD image as content; a 86box.cfg keeps its own hardware." },
    { NULL, NULL, NULL },
};

/* The values of the hardware options besides "preset" (and "none" where the
 * part can be left out), filled into option_defs[] in lr_presets_declare. A
 * part the machine cannot take is left as the preset has it. */
static const struct retro_core_option_value cpu_values[] = {
    { "4772728", "4.77 MHz" }, { "8000000", "8 MHz" }, { "10000000", "10 MHz" }, { "12000000", "12 MHz" },
    { "16000000", "16 MHz" }, { "20000000", "20 MHz" }, { "25000000", "25 MHz" }, { "33333333", "33 MHz" },
    { "40000000", "40 MHz" }, { "50000000", "50 MHz" }, { "66666666", "66 MHz" }, { "75000000", "75 MHz" },
    { "90000000", "90 MHz" }, { "100000000", "100 MHz" }, { "120000000", "120 MHz" }, { "133333333", "133 MHz" },
    { "150000000", "150 MHz" }, { "166666666", "166 MHz" }, { "200000000", "200 MHz" }, { "233333333", "233 MHz" },
    { "266666666", "266 MHz" }, { "300000000", "300 MHz" }, { "333333333", "333 MHz" }, { "350000000", "350 MHz" },
    { "400000000", "400 MHz" }, { "450000000", "450 MHz" }, { "500000000", "500 MHz" }, { "533333333", "533 MHz" },
};

static const struct retro_core_option_value mem_values[] = {
    { "256", "256 KB" }, { "512", "512 KB" }, { "640", "640 KB" }, { "1024", "1 MB" }, { "2048", "2 MB" },
    { "4096", "4 MB" }, { "8192", "8 MB" }, { "16384", "16 MB" }, { "32768", "32 MB" }, { "65536", "64 MB" },
    { "131072", "128 MB" }, { "262144", "256 MB" }, { "524288", "512 MB" }, { "1048576", "1 GB" },
};

static const struct retro_core_option_value mouse_values[] = {
    { "msserial", "Microsoft Serial Mouse" },
    { "ltserial", "Logitech Serial Mouse" },
    { "mssystems", "Mouse Systems Serial Mouse" },
    { "logibus", "Logitech/Microsoft Bus Mouse" },
    { "msbus", "Microsoft Bus Mouse (InPort)" },
    { "ps2", "PS/2 Mouse" },
};

static const struct retro_core_option_value gfx_values[] = {
    { "ati28800_5", "ATI 28800-5" },
    { "mach32_isa", "ATI Mach32 (ISA)" },
    { "ati18800", "ATI VGA Edge 16" },
    { "compaq_ega", "Compaq EGA" },
    { "v7_vga_1024i", "Headland HT208 (Video 7 VGA 1024i)" },
    { "hercules", "Hercules" },
    { "hercules_plus", "Hercules Plus" },
    { "incolor", "Hercules InColor" },
    { "cga", "IBM CGA" },
    { "ega", "IBM EGA" },
    { "mda", "IBM MDA" },
    { "vga", "IBM VGA" },
    { "oti067", "Oak OTI-067" },
    { "oti077", "Oak OTI-077" },
    { "pvga1a", "Paradise PVGA1A" },
    { "wd90c30", "Paradise WD90C30-LR" },
    { "plantronics", "Plantronics Colorplus" },
    { "tvga8900", "Trident TVGA 8900" },
    { "et3000ax", "Tseng Labs ET3000AX (ISA)" },
    { "et4000ax", "Tseng Labs ET4000AX" },
    { "mach64gx_isa", "ATI Mach64GX (Graphics Pro Turbo) (ISA)" },
    { "cl_gd5428_isa", "Cirrus Logic GD5428 (ISA)" },
    { "cl_gd5429_isa", "Cirrus Logic GD5429 (ISA)" },
    { "ht216", "Headland HT216" },
    { "wd90c31", "Paradise WD90C31A-LR" },
    { "s3_86c928_isa", "S3 86c928 ISA" },
    { "s3_86c805_isa", "S3 86c805 ISA" },
    { "et4000w32i", "Tseng Labs ET4000/w32i ISA" },
    { "mach32_vlb", "ATI Mach32 (VLB)" },
    { "mach64gx_vlb", "ATI Mach64GX (Graphics Pro Turbo) (VLB)" },
    { "cl_gd5424_vlb", "Cirrus Logic GD5424 (VLB)" },
    { "cl_gd5426_vlb", "Cirrus Logic GD5426 (VLB)" },
    { "cl_gd5429_vlb", "Cirrus Logic GD5429 (VLB)" },
    { "cl_gd5434_vlb", "Cirrus Logic GD5434 (VLB) (Orchid KELVIN 64)" },
    { "ht216_32", "Headland HT216-32 (VLB)" },
    { "s3_86c928_vlb", "S3 86c928 VLB" },
    { "s3_86c805_vlb", "S3 86c805 VLB" },
    { "s3_trio32_vlb", "S3 Trio32 VLB" },
    { "s3_trio64_vlb", "S3 Trio64 VLB" },
    { "s3_trio64vplus_vlb", "S3 Trio64V+ VLB" },
    { "s3_vision864_vlb", "S3 Vision864 VLB" },
    { "s3_vision964_vlb", "S3 Vision964 VLB" },
    { "s3_vision968_vlb", "S3 Vision968 VLB" },
    { "tgui9400cxi_vlb", "Trident TGUI 9400CXi (VLB)" },
    { "tgui9440_vlb", "Trident TGUI 9440AGi VLB" },
    { "et4000w32_vlb", "Tseng Labs ET4000/w32 VLB" },
    { "et4000w32i_vlb", "Tseng Labs ET4000/w32i VLB" },
    { "voodoo3_1k_pci", "3dfx Voodoo3 1000 (PCI)" },
    { "voodoo3_2k_pci", "3dfx Voodoo3 2000 (PCI)" },
    { "voodoo3_3k_pci", "3dfx Voodoo3 3000 (PCI)" },
    { "mach64_rage2p", "ATI 3D Rage II+ DVD (PCI)" },
    { "mach32_pci", "ATI Mach32 (PCI)" },
    { "mach64gx_pci", "ATI Mach64GX PCI" },
    { "mach64ct", "ATI Mach64CT (PCI)" },
    { "mach64vt", "ATI Mach64VT (PCI)" },
    { "mach64vt2", "ATI Mach64VT2 (PCI)" },
    { "ati_xpert128_pci", "ATI Rage 128 (Xpert 128) (PCI)" },
    { "bochs_svga", "Bochs SVGA (PCI)" },
    { "chips_69000", "Chips & Technologies B69000 (PCI)" },
    { "cl_gd5430_pci", "Cirrus Logic GD5430 (PCI)" },
    { "cl_gd5434_pci", "Cirrus Logic GD5434 (PCI)" },
    { "cl_gd5436_pci", "Cirrus Logic GD5436 (PCI)" },
    { "cl_gd5440_pci", "Cirrus Logic GD5440 (PCI)" },
    { "cl_gd5480_pci", "Cirrus Logic GD5480 (PCI)" },
    { "millennium", "Matrox Millennium (PCI)" },
    { "millennium_ii", "Matrox Millennium II (PCI)" },
    { "mystique", "Matrox Mystique (PCI)" },
    { "mystique_220", "Matrox Mystique 220 (PCI)" },
    { "s3_86c928_pci", "S3 86c928 PCI" },
    { "s3_trio32_pci", "S3 Trio32 PCI" },
    { "trio3d2x", "S3 Trio3D/2X (362) PCI" },
    { "s3_trio64_pci", "S3 Trio64 PCI" },
    { "s3_trio64vplus_pci", "S3 Trio64V+ PCI" },
    { "s3_trio64v2dx_pci", "S3 Trio64V2/DX PCI" },
    { "virge_pci", "S3 ViRGE PCI" },
    { "virge_dx_pci", "S3 ViRGE/DX PCI" },
    { "virge385_pci", "S3 ViRGE/GX PCI" },
    { "virge357_pci", "S3 ViRGE/GX2 PCI" },
    { "virge_vx_pci", "S3 ViRGE/VX PCI" },
    { "s3_vision864_pci", "S3 Vision864 PCI" },
    { "s3_vision964_pci", "S3 Vision964 PCI" },
    { "s3_vision868_pci", "S3 Vision868 PCI" },
    { "s3_vision968_pci", "S3 Vision968 PCI" },
    { "tgui9440_pci", "Trident TGUI 9440AGi PCI" },
    { "tgui9660_pci", "Trident TGUI 9660XGi PCI" },
    { "tgui9680_pci", "Trident TGUI 9680XGi PCI" },
    { "voodoo_banshee_agp", "3dfx Voodoo Banshee (AGP)" },
    { "velocity100_agp", "3dfx Velocity 100 (AGP)" },
    { "velocity200_agp", "3dfx Velocity 200 (AGP)" },
    { "voodoo3_1k_agp", "3dfx Voodoo3 1000 (AGP)" },
    { "voodoo3_2k_agp", "3dfx Voodoo3 2000 (AGP)" },
    { "voodoo3_3k_agp", "3dfx Voodoo3 3000 (AGP)" },
    { "voodoo3_3500_agp", "3dfx Voodoo3 3500 AGP" },
    { "ati_rage128_maxx", "ATI Rage Fury MAXX AGP" },
    { "ati_rage128_pro", "ATI Rage 128 Pro (Fury Pro) (AGP)" },
    { "millennium_ii_agp", "Matrox Millennium II AGP" },
    { "productiva_g100", "Matrox Productiva G100 (AGP)" },
    { "trio3d2x_agp", "S3 Trio3D/2X (362) AGP" },
    { "virge_gx2_agp", "S3 ViRGE/GX2 AGP" },
};

static const struct retro_core_option_value snd_values[] = {
    { "adlibgold", "AdLib Gold" },
    { "csm", "Covox Sound Master" },
    { "cms", "Creative Music System / Game Blaster" },
    { "ess_es488", "ESS AudioDrive ES488" },
    { "ess_es1488", "ESS AudioDrive ES1488" },
    { "ssi2001", "Innovation SSI-2001" },
    { "thunderboard", "Media Vision Thunder Board" },
    { "mmb", "Mindscape Music Board" },
    { "pas", "Media Vision Pro Audio Spectrum" },
    { "sb", "Sound Blaster v1.0" },
    { "sb1.5", "Sound Blaster v1.5" },
    { "sb2.0", "Sound Blaster v2.0" },
    { "sbprov1", "Sound Blaster Pro v1" },
    { "sbprov2", "Sound Blaster Pro v2" },
    { "Entertainer", "The Entertainer" },
    { "pssj_isa", "Tandy PSSJ Clone" },
    { "saaym", "TexElec SAAYM" },
    { "tndy", "TNDY" },
    { "adlib", "AdLib" },
    { "soundmasterplus", "Covox Sound Master Plus" },
    { "voicemasterkey", "Covox Voice Master Key" },
    { "isadacr0", "ISA DAC-r0" },
    { "isadacr1", "ISA DAC-r1" },
    { "soundman", "Rainbow Arts PC-Soundman" },
    { "acermagic_s20", "AcerMagic S20" },
    { "ad1816", "Analog Devices AD1816" },
    { "aztpr16", "Aztech Sound Galaxy Pro 16" },
    { "azt1605", "Aztech Sound Galaxy Nova 16 Extra" },
    { "azt2316a", "Aztech Sound Galaxy Pro 16 AB" },
    { "azt2316r", "Aztech Sound Galaxy Pro 16 II" },
    { "guspnp_compaq", "Compaq UltraSound 32" },
    { "sb_goldfinch", "Creative EMU8000 PnP (Goldfinch)" },
    { "cs4232", "Crystal CS4232" },
    { "cs4235", "Crystal CS4235" },
    { "cs4236b", "Crystal CS4236B" },
    { "ess_es688", "ESS AudioDrive ES688" },
    { "ess_ess0100_pnp", "ESS AudioDrive ES688 (ESS0100) PnP" },
    { "ess_ess0968_pnp_es688", "ESS AudioDrive ES688 (ESS0968) PnP" },
    { "ess_es1688", "ESS AudioDrive ES1688" },
    { "ess_ess0102_pnp", "ESS AudioDrive ES1688 (ESS0102) PnP" },
    { "ess_ess0968_pnp", "ESS AudioDrive ES1688 (ESS0968) PnP" },
    { "ess_es1868", "ESS AudioDrive ES1868" },
    { "ess_es1869", "ESS AudioDrive ES1869" },
    { "gus", "Gravis UltraSound" },
    { "gusv34", "Gravis UltraSound (rev 3.4)" },
    { "gusv37", "Gravis UltraSound (rev 3.7)" },
    { "gusmax", "Gravis UltraSound MAX" },
    { "gusace", "Gravis UltraSound ACE" },
    { "gusextreme", "Gravis UltraSound Extreme" },
    { "guspnp", "Gravis UltraSound PnP (Old)" },
    { "guspnp_new", "Gravis UltraSound PnP (New)" },
    { "guspnp_nocd", "Gravis UltraSound PnP (No CD)" },
    { "azt2320", "HP Multimedia Pro 16V-A" },
    { "pasplus", "Media Vision Pro Audio Spectrum Plus" },
    { "pas16", "Media Vision Pro Audio Spectrum 16" },
    { "pas16d", "Media Vision Pro Audio Spectrum 16D" },
    { "jazz16", "Media Vision Pro Sonic 16" },
    { "mirosound_pcm10", "miroSOUND PCM10" },
    { "opti_82c930", "OPTi 82C930" },
    { "opti_82c931", "OPTi 82C931" },
    { "sb16", "Sound Blaster 16" },
    { "sb16_pnp", "Sound Blaster 16 PnP" },
    { "sb16_pnp_ide", "Sound Blaster 16 PnP (IDE)" },
    { "sb32_pnp", "Sound Blaster 32 PnP" },
    { "sbawe32", "Sound Blaster AWE32" },
    { "sbawe32_pnp", "Sound Blaster AWE32 PnP" },
    { "sbawe32_ide_pnp", "Sound Blaster AWE32 IDE PnP" },
    { "sbawe64_value", "Sound Blaster AWE64 Value" },
    { "sbawe64", "Sound Blaster AWE64" },
    { "sbawe64_ide", "Sound Blaster AWE64 (IDE)" },
    { "sbawe64_gold", "Sound Blaster AWE64 Gold" },
    { "sb_vibra16c", "Sound Blaster ViBRA 16C" },
    { "sb_vibra16cl", "Sound Blaster ViBRA 16CL" },
    { "sb_vibra16s", "Sound Blaster ViBRA 16S" },
    { "sb_vibra16xv", "Sound Blaster ViBRA 16XV" },
    { "gusvipermax", "Synergy ViperMAX" },
    { "wss", "Windows Sound System" },
    { "ymf701", "Yamaha YMF-701 (OPL3-SA)" },
    { "ymf718", "Yamaha YMF718 (OPL3-SA2)" },
    { "ymf719", "Yamaha YMF719 (OPL3-SA3)" },
    { "cmi8338", "C-Media CMI8338" },
    { "cmi8738", "C-Media CMI8738" },
    { "es1370", "Ensoniq AudioPCI (ES1370)" },
    { "es1371", "Ensoniq AudioPCI (ES1371)" },
    { "ess_solo1", "ESS Solo-1 ES1938S" },
    { "es1373", "Sound Blaster PCI 128 (ES1373)" },
    { "ct5880", "Sound Blaster PCI 4.1 (CT5880)" },
};

static const struct retro_core_option_value nic_values[] = {
    { "3c501", "3Com EtherLink (3c500/3c501)" },
    { "3c503", "3Com EtherLink II" },
    { "3c509b", "3Com EtherLink III ISA (3C509B)" },
    { "ne1k", "NE1000 Compatible" },
    { "ne2k8", "NE2000 Compatible 8-bit" },
    { "novell_ne1k", "Novell NE1000" },
    { "novell_ne2k", "Novell NE2000" },
    { "ne2kpnp", "Realtek RTL8019AS" },
    { "wd8003e", "Western Digital WD8003E" },
    { "wd8003eb", "Western Digital WD8003EB" },
    { "wd8013ebt", "Western Digital WD8013EBT" },
    { "pcnetisa", "AMD PCnet-ISA" },
    { "pcnetisaplus", "AMD PCnet-ISA+" },
    { "de220p", "D-Link DE-220P" },
    { "ne2k", "NE2000 Compatible" },
    { "pcnetracal", "Racal Interlan EtherBlaster" },
    { "pcnetvlb", "AMD PCnet-32" },
    { "pcnetfast", "AMD PCnet-FAST III" },
    { "pcnetpci", "AMD PCnet-PCI II" },
    { "dp83815", "National Semiconductor DP83815/16" },
    { "i82557", "Intel PRO/100" },
    { "i82558", "Intel PRO/100+" },
    { "nec_pk_ug_x006", "NEC PK-UG-X006" },
    { "ne2kpci", "Realtek RTL8029AS" },
    { "rtl8139c+", "Realtek RTL8139C+" },
};


static struct retro_core_option_v2_definition option_defs[] = {
    {
        "86box_machine", "Machine (Reset)", NULL,
        "The PC to build when the content is a disk, floppy or CD image: a preset, or \"any machine\" of a group, picked in the All Machines submenu. A change takes effect at the next Reset (Restart Content) or start. A 86box.cfg as content keeps its own machine.",
        NULL, NULL,
        { { NULL, NULL } }, /* filled from presets[] and groups[] in lr_presets_declare */
        DEFAULT_PRESET
    },
    {
        "86box_all_8088", "8088 / 8086", NULL,
        "Used when Machine is \"8088 / 8086 - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_286", "286", NULL,
        "Used when Machine is \"286 - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_386sx", "386SX", NULL,
        "Used when Machine is \"386SX - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_386dx", "386DX", NULL,
        "Used when Machine is \"386DX - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_486", "486", NULL,
        "Used when Machine is \"486 - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_486pci", "486 PCI", NULL,
        "Used when Machine is \"486 PCI - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_socket45", "Pentium (Socket 4, 5)", NULL,
        "Used when Machine is \"Pentium (Socket 4, 5) - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_socket7", "Pentium MMX, K6 (Socket 7)", NULL,
        "Used when Machine is \"Pentium MMX, K6 (Socket 7) - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_ss7", "K6-2, K6-III (Super Socket 7)", NULL,
        "Used when Machine is \"K6-2, K6-III (Super Socket 7) - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_all_p6", "Pentium Pro, II, Celeron (Socket 8, Slot 1, Socket 370)", NULL,
        "Used when Machine is \"Pentium Pro, II, Celeron (Socket 8, Slot 1, Socket 370) - any machine\". The graphics and sound card, hard disk controller and mouse are picked by the board's bus; the Hardware options change them.",
        NULL, "machines",
        { { NULL, NULL } }, /* filled from the machine table in lr_presets_declare */
        NULL
    },
    {
        "86box_new_hdd", "New Hard Disk (Reset)", NULL,
        "With a CD or floppy image as content, a blank hard disk of this size is made beside the machine's configuration (in saves/86box/<content>), for installing to. It is made once and kept. Over 504 MB needs a BIOS with LBA (486 and later), over 8 GB a Pentium-era one and Windows 95 OSR2 or later (FAT32); on Linux and macOS the file only takes the space written to it, on Windows the whole size.",
        NULL, NULL,
        {
            { "none", "None" },
            { "504", "504 MB" },
            { "2048", "2 GB" },
            { "8064", "8 GB" },
            { "16128", "16 GB" },
            { "32256", "32 GB" },
            { "65024", "64 GB" },
            { "130048", "127 GB" },
            { NULL, NULL },
        },
        "2048"
    },
    {
        "86box_cpu_speed", "CPU Speed", NULL,
        "The CPU speed within the preset's CPU family; the nearest faster one the board takes, or its fastest.",
        NULL, "hardware",
        { { NULL, NULL } }, /* filled in lr_presets_declare */
        "preset"
    },
    {
        "86box_mem", "Memory", NULL,
        "Memory, within what the board takes.",
        NULL, "hardware",
        { { NULL, NULL } },
        "preset"
    },
    {
        "86box_gfxcard", "Graphics Card", NULL,
        "A graphics card for the board's bus (ISA, VLB, PCI or AGP). Machines with fixed built-in graphics keep theirs.",
        NULL, "hardware",
        { { NULL, NULL } },
        "preset"
    },
    {
        "86box_voodoo", "3dfx Voodoo Graphics", NULL,
        "A Voodoo Graphics card beside the graphics card, for Glide and early 3D games; PCI machines only.",
        NULL, "hardware",
        {
            { "preset", "Preset" },
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "preset"
    },
    {
        "86box_sndcard", "Sound Card", NULL,
        "A sound card for the board's bus.",
        NULL, "hardware",
        { { NULL, NULL } },
        "preset"
    },
    {
        "86box_mouse", "Mouse", NULL,
        "The mouse the machine has. DOS needs a mouse driver for it; a serial mouse is on COM1.",
        NULL, "hardware",
        { { NULL, NULL } },
        "preset"
    },
    {
        "86box_nic", "Network Card", NULL,
        "A network card, connected through RetroArch's host to the internet (SLiRP, outgoing connections only).",
        NULL, "hardware",
        { { NULL, NULL } },
        "none"
    },
    {
        "86box_dynarec", "Dynamic Recompiler (Reset)", NULL,
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

static struct retro_core_option_v2_definition *
option_def(const char *key)
{
    for (struct retro_core_option_v2_definition *d = option_defs; d->key; d++)
        if (!strcmp(d->key, key))
            return d;
    return NULL;
}

/* "preset" (and "none"), then the list, as many as fit */
static void
fill_values(const char *key, bool none, const struct retro_core_option_value *list, size_t n)
{
    struct retro_core_option_v2_definition *d = option_def(key);
    size_t i = 0;
    d->values[i++] = (struct retro_core_option_value) { "preset", "Preset" };
    if (none)
        d->values[i++] = (struct retro_core_option_value) { "none", "None" };
    for (size_t j = 0; j < n && i < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1; j++)
        d->values[i++] = list[j];
    d->values[i] = (struct retro_core_option_value) { NULL, NULL };
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

void
lr_presets_declare(retro_environment_t cb)
{
    for (size_t i = 0; i < NPRESETS && i < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1; i++) {
        option_defs[0].values[i].value = presets[i].key;
        option_defs[0].values[i].label = presets[i].label;
    }
    size_t n = NPRESETS;
    static char group_values[NGROUPS][16], group_labels[NGROUPS][96];
    for (size_t g = 0; g < NGROUPS && n < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1; g++, n++) {
        snprintf(group_values[g], sizeof(group_values[g]), "all_%s", groups[g].key);
        snprintf(group_labels[g], sizeof(group_labels[g]), "%s - any machine", groups[g].label);
        option_defs[0].values[n] = (struct retro_core_option_value) { group_values[g], group_labels[g] };
    }
    option_defs[0].values[n].value = NULL;

    /* The machine table, in its order (by chipset), into its group's list */
    for (size_t g = 0; g < NGROUPS; g++) {
        char key[32];
        snprintf(key, sizeof(key), "86box_all_%s", groups[g].key);
        struct retro_core_option_v2_definition *d = option_def(key);
        size_t i = 0;
        for (int m = 0; machine_get_internal_name_ex(m) && i < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1; m++) {
            const int type = machine_get_type(m);
            if (type >= groups[g].first_type && type <= groups[g].last_type)
                d->values[i++] = (struct retro_core_option_value) { machine_get_internal_name_ex(m), machine_getname(m) };
        }
        d->values[i]     = (struct retro_core_option_value) { NULL, NULL };
        d->default_value = d->values[0].value;
    }
    fill_values("86box_cpu_speed", false, cpu_values, ARRAY_SIZE(cpu_values));
    fill_values("86box_mem", false, mem_values, ARRAY_SIZE(mem_values));
    fill_values("86box_gfxcard", false, gfx_values, ARRAY_SIZE(gfx_values));
    fill_values("86box_sndcard", true, snd_values, ARRAY_SIZE(snd_values));
    fill_values("86box_mouse", true, mouse_values, ARRAY_SIZE(mouse_values));
    /* No network card unless asked for: "none" first, without "preset" */
    fill_values("86box_nic", false, nic_values, ARRAY_SIZE(nic_values));
    option_def("86box_nic")->values[0] = (struct retro_core_option_value) { "none", "None" };

    unsigned version = 0;
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2)
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options);
    else {
        /* A frontend without v2 options: the plain variables, "Label; default|other|..." */
        static struct retro_variable vars[ARRAY_SIZE(option_defs)];
        static char descs[ARRAY_SIZE(option_defs)][4096];
        size_t v = 0;
        for (const struct retro_core_option_v2_definition *d = option_defs; d->key; d++, v++) {
            size_t n = (size_t) snprintf(descs[v], sizeof(descs[v]), "%s; %s", d->desc, d->default_value);
            for (const struct retro_core_option_value *o = d->values; o->value && n < sizeof(descs[v]); o++)
                if (strcmp(o->value, d->default_value))
                    n += (size_t) snprintf(descs[v] + n, sizeof(descs[v]) - n, "|%s", o->value);
            vars[v] = (struct retro_variable) { d->key, descs[v] };
        }
        vars[v] = (struct retro_variable) { NULL, NULL };
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

/* The first of names (video, sound or mouse internal names) machine m takes */
typedef enum { PART_VIDEO, PART_SOUND, PART_MOUSE } part_kind_t;

static bool
part_fits(part_kind_t kind, const char *name, int m)
{
    int c;
    switch (kind) {
        case PART_VIDEO:
            c = video_get_video_from_internal_name((char *) name);
            return c > 0 && device_is_valid(video_card_getdevice(c), m);
        case PART_SOUND:
            c = sound_card_get_from_internal_name(name);
            return c > 0 && device_is_valid(sound_card_getdevice(c), m);
        default:
            c = mouse_get_from_internal_name((char *) name);
            return c > 0 && device_is_valid(mouse_get_device(c), m);
    }
}

static const char *
first_fitting(part_kind_t kind, const char *const *names, int m)
{
    for (; *names; names++)
        if (part_fits(kind, *names, m))
            return *names;
    return "none";
}

/* A preset for any machine of the table: the fastest CPU it takes (of the
 * family that has it, the earlier one on a tie), and parts by its bus and what
 * it has built in */
static const preset_t *
any_machine(const char *name, const group_t *g)
{
    static preset_t pr;
    const int       m = machine_get_machine_from_internal_name(name);

    const char *family = NULL;
    uint32_t    fastest = 0;
    for (int f = 0; cpu_families[f].package; f++) {
        if (!cpu_family_is_eligible(&cpu_families[f], m))
            continue;
        for (int c = 0; cpu_families[f].cpus[c].cpu_type; c++)
            if (cpu_families[f].cpus[c].rspeed > fastest && cpu_is_eligible(&cpu_families[f], c, m)) {
                fastest = cpu_families[f].cpus[c].rspeed;
                family  = cpu_families[f].internal_name;
            }
    }

    long       mem = g->mem_kb;
    const long min = machine_get_min_ram(m), gran = machine_get_ram_granularity(m);
    if (mem > machine_get_max_ram(m))
        mem = machine_get_max_ram(m);
    if (mem < min)
        mem = min;
    else if (gran > 0)
        mem = min + (mem - min) / gran * gran;

    /* The first one the board takes; a PC/XT gets CGA and a Sound Blaster Pro,
     * a 286 or 386 the Sound Blaster Pro rather than the 16 */
    static const char *const gfx[]    = { "voodoo3_3k_agp", "virge_dx_pci", "cl_gd5429_vlb", "et4000ax", "ibm1mbsvga", NULL };
    static const char *const gfx_xt[] = { "cga", NULL };
    static const char *const snd[]    = { "sb16", "sbprov2", "es1371", "sbpromcv", "adlib", NULL };
    static const char *const mouse[]  = { "ps2", "msserial", "logibus", NULL };

    const bool at  = machine_has_bus(m, MACHINE_BUS_ISA16);
    const bool ide = machine_has_flags(m, MACHINE_IDE_PRI);
    pr = (preset_t) {
        name, machine_getname(m), name, family ? family : "", 2000000000, (uint32_t) mem,
        machine_has_flags(m, MACHINE_VIDEO) ? "internal" : first_fitting(PART_VIDEO, g->first_type < MACHINE_TYPE_286 ? gfx_xt : gfx, m),
        0,
        machine_has_flags(m, MACHINE_SOUND) ? "internal" : first_fitting(PART_SOUND, snd + (g->first_type < MACHINE_TYPE_SOCKET1), m),
        machine_has_flags(m, MACHINE_MOUSE) ? "internal" : first_fitting(PART_MOUSE, mouse, m),
        ide ? "internal" : at ? "ide_isa_2ch" : machine_has_bus(m, MACHINE_BUS_ISA) ? "xtide" : NULL,
        g->first_type >= MACHINE_TYPE_286 ? "35_2hd" : "525_2dd",
        at ? "525_2hd" : NULL,
        machine_has_flags(m, MACHINE_IDE_SEC) || (!ide && at)
    };
    return &pr;
}

/* What the hardware options change in the preset, for machine m. A part the
 * machine cannot take stays as the preset has it, and is named in skipped. */
typedef struct {
    const char *gfxcard;
    int         voodoo;
    const char *sndcard;
    const char *mouse;
    const char *nic; /* NULL for none */
    uint32_t    cpu_speed;
    uint32_t    mem_kb;
} parts_t;

static void
skip_part(char *skipped, size_t size, const char *what)
{
    size_t n = strlen(skipped);
    snprintf(skipped + n, size - n, "%s%s", n ? ", " : "", what);
}

static void
pick_parts(retro_environment_t cb, const preset_t *pr, int m, parts_t *out, char *skipped, size_t size)
{
    *out = (parts_t) { pr->gfxcard, pr->voodoo, pr->sndcard, pr->mouse, NULL, pr->cpu_speed, pr->mem_kb };
    skipped[0] = '\0';

    const char *v = option(cb, "86box_gfxcard", "preset");
    if (strcmp(v, "preset")) {
        const int c = video_get_video_from_internal_name((char *) v);
        if (c > 0 && device_is_valid(video_card_getdevice(c), m) && !machine_has_flags_64(m, MACHINE_VIDEO_ONLY))
            out->gfxcard = v;
        else
            skip_part(skipped, size, "graphics card");
    }

    v = option(cb, "86box_voodoo", "preset");
    if (strcmp(v, "preset")) {
        const int on = !strcmp(v, "enabled");
        if (!on || machine_has_bus(m, MACHINE_BUS_PCI))
            out->voodoo = on;
        else
            skip_part(skipped, size, "Voodoo");
    }

    v = option(cb, "86box_sndcard", "preset");
    if (strcmp(v, "preset")) {
        const int c = sound_card_get_from_internal_name(v);
        if (!strcmp(v, "none") || (c > 0 && device_is_valid(sound_card_getdevice(c), m)))
            out->sndcard = v;
        else
            skip_part(skipped, size, "sound card");
    }

    v = option(cb, "86box_mouse", "preset");
    if (strcmp(v, "preset")) {
        const int c = mouse_get_from_internal_name((char *) v);
        if (!strcmp(v, "none") || (c > 0 && device_is_valid(mouse_get_device(c), m)))
            out->mouse = v;
        else
            skip_part(skipped, size, "mouse");
    }

    v = option(cb, "86box_nic", "none");
    if (strcmp(v, "none")) {
        const int c = network_card_get_from_internal_name((char *) v);
        if (c > 0 && device_is_valid(network_card_getdevice(c), m))
            out->nic = v;
        else
            skip_part(skipped, size, "network card");
    }

    /* 86Box takes the nearest faster CPU of the family, or its fastest */
    v = option(cb, "86box_cpu_speed", "preset");
    if (strcmp(v, "preset"))
        out->cpu_speed = (uint32_t) strtoul(v, NULL, 10);

    v = option(cb, "86box_mem", "preset");
    if (strcmp(v, "preset")) {
        long       mem  = strtol(v, NULL, 10);
        const long min  = machine_get_min_ram(m);
        const long gran = machine_get_ram_granularity(m);
        if (mem > machine_get_max_ram(m))
            mem = machine_get_max_ram(m);
        if (mem < min)
            mem = min;
        else if (gran > 0)
            mem = min + (mem - min) / gran * gran;
        out->mem_kb = (uint32_t) mem;
    }
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
    /* 86Box's limit for IDE, 127 GB (28-bit LBA) */
    if (cyl > 266305)
        cyl = 266305;
    fprintf(f, "hdd_%02i_parameters = 63, 16, %lld, 0, ide\n", n, cyl);
    fprintf(f, "hdd_%02i_fn = %s\n", n, path);
    fprintf(f, "hdd_%02i_ide_channel = %s\n", n, channel);
}

/* The options a configuration is made from, as one string: a change of any
 * of them makes Reset build the machine again */
static char made_from[1024];

static void
options_signature(retro_environment_t cb, char *out, size_t size)
{
    static const char *const keys[] = { "86box_machine", "86box_cpu_speed", "86box_mem", "86box_gfxcard",
                                        "86box_voodoo", "86box_sndcard", "86box_mouse", "86box_nic",
                                        "86box_new_hdd", "86box_dynarec" };
    size_t len = 0;
    out[0]     = '\0';
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]) && len < size; i++)
        len += snprintf(out + len, size - len, "%s;", option(cb, keys[i], ""));
    for (size_t g = 0; g < NGROUPS && len < size; g++) {
        char opt[32];
        snprintf(opt, sizeof(opt), "86box_all_%s", groups[g].key);
        len += snprintf(out + len, size - len, "%s;", option(cb, opt, ""));
    }
}

bool
lr_presets_changed(retro_environment_t cb)
{
    if (!made_from[0])   /* a 86box.cfg as content: its own machine */
        return false;
    char now[sizeof(made_from)];
    options_signature(cb, now, sizeof(now));
    return strcmp(now, made_from) != 0;
}

bool
lr_presets_make_config(retro_environment_t cb, const char *content, char *cfg_path, size_t cfg_size, bool keep_media)
{
    made_from[0] = '\0';
    if (lr_ext_is(lr_extension(content), "cfg")) {
        snprintf(cfg_path, cfg_size, "%s", content);
        return true;
    }
    options_signature(cb, made_from, sizeof(made_from));

    const char *key    = option(cb, "86box_machine", DEFAULT_PRESET);
    const preset_t *pr = &presets[0];
    for (size_t i = 0; i < NPRESETS; i++)
        if (!strcmp(presets[i].key, key))
            pr = &presets[i];
    for (size_t g = 0; g < NGROUPS; g++)
        if (!strncmp(key, "all_", 4) && !strcmp(key + 4, groups[g].key)) {
            char opt[32];
            snprintf(opt, sizeof(opt), "86box_all_%s", groups[g].key);
            const char *name = option(cb, opt, NULL);
            if (name && machine_get_machine_from_internal_name(name) >= 0)
                pr = any_machine(name, &groups[g]);
        }

    /* What the content holds (an image, or an .m3u's): the floppy and CD in
     * the drives at the start, and a hard disk */
    if (!keep_media)
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
    parts_t parts;
    char    skipped[100];
    pick_parts(cb, pr, machine_get_machine_from_internal_name(pr->machine), &parts, skipped, sizeof(skipped));
    fprintf(f, "# Made by the libretro core from its \"%s\" machine preset; rewritten at each start.\n"
               "# For a machine of your own, load a 86box.cfg as content instead.\n\n", pr->label);
    fprintf(f, "[Machine]\nmachine = %s\ncpu_family = %s\ncpu_speed = %u\nmem_size = %u\ncpu_use_dynarec = %d\ntime_sync = local\n\n",
            pr->machine, pr->cpu_family, parts.cpu_speed, parts.mem_kb, dynarec ? 1 : 0);
    fprintf(f, "[Video]\ngfxcard = %s\nvoodoo = %d\n\n", parts.gfxcard, parts.voodoo);
    fprintf(f, "[Input devices]\nmouse_type = %s\n\n", parts.mouse);
    fprintf(f, "[Sound]\nsndcard = %s\n\n", parts.sndcard);
    if (parts.nic)
        fprintf(f, "[Network]\nnet_01_card = %s\nnet_01_net_type = slirp\n\n", parts.nic);
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
    } else if (skipped[0]) {
        char text[300];
        snprintf(text, sizeof(text), "The %s machine cannot take that %s; it keeps the preset's", pr->label, skipped);
        lr_message(text, 600);
    }
    return true;
}
