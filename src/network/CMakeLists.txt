#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2024      Jasmine Iwanek.
#
set(net_sources)
list(APPEND net_sources
    network.c
    net_pcap.c
    net_slirp.c
    net_switch.c
    net_dp8390.c
    net_3c501.c
    net_3c503.c
    net_3c509b.c
    net_3c59x_eisa.c
    net_ne2000.c
    net_pcnet.c
    net_wd8003.c
    net_pe3.c
    net_plip.c
    net_event.c
    net_null.c
    net_tulip.c
    net_smc_epic100.c
    net_rtl8139.c
    net_dp83815.c
    net_i8255x.c
    net_l80225.c
    net_modem.c
    utils/getline.c
)

find_package(PkgConfig REQUIRED)
if(LIBRETRO)
    # The core is a single .so loaded into RetroArch: libslirp is seldom
    # installed where RetroArch runs, and the buildbot images have none, so
    # it is built into the core from src/libretro/deps/slirp. Its glib stays
    # shared, as nearly every Linux system has it. link.T keeps slirp's
    # symbols out of the core's exports.
    set(SLIRP_DIR ${CMAKE_SOURCE_DIR}/src/libretro/deps/slirp)
    file(GLOB SLIRP_SOURCES ${SLIRP_DIR}/src/*.c)
    add_library(slirp_bundled STATIC ${SLIRP_SOURCES})
    set_target_properties(slirp_bundled PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(slirp_bundled PRIVATE G_LOG_DOMAIN="Slirp")
    target_include_directories(slirp_bundled PUBLIC ${SLIRP_DIR}/include)
    pkg_check_modules(SLIRP_GLIB REQUIRED IMPORTED_TARGET glib-2.0)
    if(WIN32)
        # glib built into the .dll as well: a Windows RetroArch has no glib
        target_compile_definitions(slirp_bundled PUBLIC GLIB_STATIC_COMPILATION)
        target_include_directories(slirp_bundled PUBLIC ${SLIRP_GLIB_STATIC_INCLUDE_DIRS})
        target_link_directories(slirp_bundled PUBLIC ${SLIRP_GLIB_STATIC_LIBRARY_DIRS})
        target_link_libraries(slirp_bundled PUBLIC ${SLIRP_GLIB_STATIC_LIBRARIES} iconv)
    else()
        target_link_libraries(slirp_bundled PUBLIC PkgConfig::SLIRP_GLIB)
    endif()
    if(WIN32)
        target_link_libraries(slirp_bundled PUBLIC ws2_32 iphlpapi)
    elseif(APPLE)
        target_link_libraries(slirp_bundled PUBLIC resolv)
    endif()
    target_link_libraries(86Box slirp_bundled)
else()
    pkg_check_modules(SLIRP REQUIRED IMPORTED_TARGET slirp)
    target_link_libraries(86Box PkgConfig::SLIRP)

    if(WIN32)
        target_link_libraries(PkgConfig::SLIRP INTERFACE wsock32 ws2_32 iphlpapi)
        if (NOT MSVC)
            target_link_libraries(PkgConfig::SLIRP INTERFACE iconv)
        endif()
        if(STATIC_BUILD)
            add_compile_definitions(LIBSLIRP_STATIC)
        endif()
    endif()
endif()

if (HAIKU)
    target_link_libraries(86Box network)
endif()

if(WIN32)
    target_link_libraries(86Box ws2_32)
endif()

if (UNIX)
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	set_source_files_properties(net_slirp.c PROPERTIES COMPILE_FLAGS "-I/usr/local/include")
    endif()
    find_path(HAS_VDE "libvdeplug.h" PATHS ${VDE_INCLUDE_DIR} "/usr/include /usr/local/include" "/opt/homebrew/include" )
    if(HAS_VDE)
        find_library(VDE_LIB vdeplug)
        if (NOT VDE_LIB)
            message(WARNING "Could not find VDE. The library will not be bundled and any related features will be disabled.")
        else()
            add_compile_definitions(HAS_VDE)
            list(APPEND net_sources net_vde.c)
        endif()
    endif()
endif()
if (UNIX AND NOT APPLE) # Support for TAP on Linux and BSD, supposedly.
    find_path(HAS_TAP "linux/if_tun.h" PATHS ${TAP_INCLUDE_DIR} "/usr/include /usr/local/include" "/opt/homebrew/include" )
    if(HAS_TAP)
        add_compile_definitions(HAS_TAP)
        list(APPEND net_sources net_tap.c)
    else()
        message(WARNING "TAP support not available. Are you on some BSD?")
    endif()
endif()

add_library(net OBJECT ${net_sources})
if(LIBRETRO)
    # <slirp/libslirp.h> from the bundled copy
    target_link_libraries(net PRIVATE slirp_bundled)
endif()
