(We need help in developing this emulator! See https://github.com/86Box/86Box/issues/7386 for details. Any and all help is appreciated.)

86Box
=====

[![Build Status](https://ci.86box.net/job/86Box/badge/icon)](https://ci.86box.net/job/86Box/)
[![License](https://img.shields.io/github/license/86Box/86Box)](COPYING)
[![Latest release](https://img.shields.io/github/release/86Box/86Box.svg)](https://github.com/86Box/86Box/releases)
[![Downloads](https://img.shields.io/github/downloads/86Box/86Box/total.svg)](https://github.com/86Box/86Box/releases)
[![Translation status](https://weblate.86box.net/widget/86box/86box/language-badge.svg)](https://weblate.86box.net/engage/86box/)

libretro core (this fork)
-------------------------

This fork builds 86Box as a libretro core, `86box_libretro`, for RetroArch and other libretro frontends. The frontend is in `src/libretro`, built with `-D LIBRETRO=ON`.

* **Download:** [releases](https://github.com/WizzardSK/86box-libretro/releases) (Linux x86_64 and aarch64). Put `86box_libretro.so` in RetroArch's cores folder.
* **ROMs:** the 86Box ROM set ([86Box/roms](https://github.com/86Box/roms)) goes into `system/86box/roms`.
* **Content:** a disk, floppy or CD image (`.img`, `.vhd`, `.iso`, `.cue`, `.chd`, `.ima`, ...), or a machine's own `86box.cfg`, as 86Box writes it (its disks are where that file says). A file with only `[Machine]` `machine = ibmpc82` and `[Video]` `gfxcard = cga` boots an IBM PC 5150 into Cassette BASIC.
* **Machine:** for an image, the core option *Machine* picks one of 33 presets, from an IBM PC 5150 (8088, CGA), PCjr, Compaq Portable, Tandy 1000 SX, Schneider EuroPC and Amstrad PC1512 through the IBM PC/AT, a Toshiba T3100e, IBM PS/1, Amstrad MegaPC, 386 and 486 machines, Pentiums from a Packard Bell PB520R, IBM PC 330, Compaq Presario 4500, Sony VAIO, Gateway 2000 and Dell Dimension XPS, a Cyrix 6x86MX, a Pentium Pro, AMD K6-2 and K6-III, Pentium IIs and Microsoft Virtual PC's hardware to a Celeron 533; each is a board with its ROM in the 86Box ROM set, with a CPU, memory, graphics and sound card of its time. Every other machine 86Box emulates is in the *All Machines* submenu, one list per CPU group (8088/8086, 286, 386SX, 386DX, 486, 486 PCI, Socket 4/5, Socket 7, Super Socket 7, Socket 8/Slot 1/Socket 370): set *Machine* to that group's "any machine" entry, and the core gives the board the fastest CPU it takes, and a graphics card, sound card, hard disk controller and mouse that fit its bus; a machine whose ROM is missing from the ROM set is replaced by another, with a message. The *Hardware* options change parts of the preset: CPU speed, memory, graphics card (about 100, ISA to AGP), a 3dfx Voodoo, sound card (about 90), mouse and a network card (SLiRP, outgoing connections); a part the board cannot take, such as a PCI card in an ISA machine, stays as the preset has it, with a message. A hard disk image goes on the primary IDE channel, a CD image in the CD-ROM drive (secondary channel), a floppy image in drive A:. With a CD or floppy, *New Hard Disk* makes a blank hard disk (504 MB to 127 GB; on Linux and macOS the file only takes the space written to it, on Windows the whole size) to install to. The configuration, the new disk and the machine's CMOS settings are kept in RetroArch's saves folder, in `86box/<content name>/` (under the core's own folder when RetroArch sorts saves by core). *Dynamic Recompiler* switches the CPU recompiler.
* **Disc Control:** an `.m3u` as content lists several images (one per line, relative to the `.m3u`): the first floppy goes in drive A:, the first CD in the CD-ROM drive and a hard disk on the primary channel, and the floppies and CDs can then be swapped from RetroArch's Disc Control menu - eject, pick an image, close the tray, and it goes into the drive of its kind. Images can also be appended there from the file browser, e.g. the second floppy of an installer or a driver CD. The machine's folder is named after the `.m3u`.
* **Input:** RetroArch's keyboard and mouse (port 1) go to the emulated machine; the RetroPads appear to 86Box as joysticks "RetroPad 1" to "RetroPad 4", which the machine's game port configuration in `86box.cfg` can use.
* **Libraries:** glib, freetype, libpng, libsndfile and zlib from the system; libslirp (`src/libretro/deps/slirp`) and zstd are built into the core.
* **Not yet:** core options, savestates, Windows, macOS and Android builds.
* **Buildbot:** `.gitlab-ci.yml` builds Linux x64 and aarch64 on the libretro buildbot; its images still need `libsndfile1-dev`. `.github/workflows/buildbot-preflight.yml` runs the same build on GitHub.

**86Box** is a low level x86 emulator that runs older operating systems and software designed for IBM PC systems and compatibles from 1981 through fairly recent system designs based on the PCI bus.

Features
--------

* Easy to use interface inspired by mainstream hypervisor software
* Low level emulation of 8086-based processors up to the Mendocino-era Celeron with focus on accuracy
* Great range of customizability of virtual machines
* Many available systems, such as the very first IBM PC 5150 from 1981, or the more obscure IBM PS/2 line of systems based on the Micro Channel Architecture
* Lots of supported peripherals including video adapters, sound cards, network adapters, hard disk controllers, and SCSI adapters
* MIDI output to Windows built-in MIDI support, FluidSynth, or emulated Roland synthesizers
* Supports running MS-DOS, older Windows versions, OS/2, many Linux distributions, or vintage systems such as BeOS or NEXTSTEP, and applications for these systems

Minimum system requirements and recommendations
-----------------------------------------------

* 64-bit Intel Core 2, AMD Athlon 64 or ARMv8 processor or newer
* 4 GB of RAM or higher
* **Windows version:** Windows 7 Service Pack 1 or newer on Intel/AMD systems; Windows 11 or newer on ARM systems
* **Linux version:** Ubuntu 16.04, Debian 9.0 or other distributions from 2016 onwards
* **macOS version:** macOS 10.14 Mojave or newer

Performance may vary depending on host and guest configuration. Most emulation logic is executed in a single thread. Therefore, systems with greater IPC (instructions per clock) capacity should be able to emulate higher clock speeds.

While 86Box already includes a manager for easy handling of multiple machines, some third-party ones exist as well:

* [Avalonia 86](https://github.com/notBald/Avalonia86) by [notBald](https://github.com/notBald) (Windows and Linux)
* [86Box Manager](https://github.com/86Box/86BoxManager) by [Overdoze](https://github.com/daviunic) (Windows only)
* [86Box Manager X](https://github.com/RetBox/86BoxManagerX) by [xafero](https://github.com/xafero) (Cross platform Port of 86Box Manager using Avalonia)
* [sl86](https://github.com/DDXofficial/sl86) by [DDX](https://github.com/DDXofficial) (Command-line 86Box machine manager written in Python)
* [Linbox-qt5](https://github.com/Dungeonseeker/linbox-qt5) by [Dungeonseeker](https://github.com/Dungeonseeker/) (Linux focused, should work on Windows though untested)
* [MacBox for 86Box](https://github.com/Moonif/MacBox) by [Moonif](https://github.com/Moonif) (MacOS only)

To manually use 86Box with multiple machines without a manager, use the `--vmpath`/`-P` command line option.

Getting started
---------------

See [our documentation](https://86box.readthedocs.io/en/latest/index.html) for an overview of the emulator's features and user interface.

Community
---------

We operate an IRC channel and a Discord server for discussing 86Box, its development, and anything related to retro computing. We look forward to hearing from you!

[![Visit our IRC channel](https://kiwiirc.com/buttons/irc.ringoflightning.net/86Box.png)](https://kiwiirc.com/client/irc.ringoflightning.net/?nick=86box|?#86Box)

[![Visit our Discord server](https://discordapp.com/api/guilds/262614059009048590/embed.png)](https://discord.gg/QXK9XTv)

[Forum: SoftHistory](https://forum.softhistory.org/)

[Wiki: SoftHistory](https://wiki.softhistory.org/)

[Twitter: @86BoxEmulator](https://twitter.com/86BoxEmulator)

[YouTube: 86Box](https://youtube.com/c/86Box)

Contributions
-------------

We welcome all contributions to the project, as long as the [contribution guidelines](CONTRIBUTING.md) are followed.

Building
---------
For instructions on how to build 86Box from source, see the [build guide](https://86box.readthedocs.io/en/latest/dev/buildguide.html).

Licensing
---------

86Box is released under the [GNU General Public License, version 2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) or later. For more information, see the `COPYING` file in the root of the repository.

The emulator can also optionally make use of [munt](https://github.com/munt/munt), [FluidSynth](https://www.fluidsynth.org/), [Ghostscript](https://www.ghostscript.com/) and [Discord Game SDK](https://discord.com/developers/docs/game-sdk/sdk-starter-guide), which are distributed under their respective licenses.

Donations
---------

We do not charge you for the emulator but donations are still welcome:
<https://paypal.me/86Box>.
The libretro core can be supported on Ko-fi:
<https://ko-fi.com/wizzardsk>.

Acknowledgments
---------------

### Powered by
[![JetBrains logo.](https://resources.jetbrains.com/storage/products/company/brand/logos/jetbrains.svg)](https://jb.gg/OpenSource)
