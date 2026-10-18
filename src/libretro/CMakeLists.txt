#
# 86Box libretro core: the frontend, in place of src/qt or src/unix.
#

add_library(plat OBJECT
    lr_plat.c
    lr_sound.c
    ../unix/unix_netsocket.c
    ../unix/unix_dynld.c
    ../unix/dummy_cdrom_ioctl.c
    ../unix/dummy_floppy_ioctl.c
)
target_compile_definitions(plat PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(plat ${CMAKE_DL_LIBS})
if(NOT CPPTHREADS)
    target_sources(plat PRIVATE ../unix/unix_thread.c)
endif()

add_library(ui OBJECT
    lr_main.c
    lr_presets.c
    ../unix/sdl_cdrom.c
)

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(86Box Threads::Threads m)

# Only the libretro API is exported: the emulator's own symbols stay inside the
# core, so they cannot bind to a frontend's libraries of the same names
if(NOT APPLE AND NOT WIN32)
    target_link_options(86Box PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/link.T" "-Wl,--no-undefined")
endif()

# The buildbot's CMake templates build the target <corename>_libretro
add_custom_target(86box_libretro DEPENDS 86Box)
