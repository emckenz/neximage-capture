cmake_minimum_required(VERSION 3.22.1)
project(neximage_probe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── libusb (manual Android build — no upstream CMake) ───────────────────────
FetchContent_Declare(
    libusb
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG v1.0.27
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(libusb)
if(NOT libusb_POPULATED)
    FetchContent_Populate(libusb)
endif()

set(LIBUSB_CONFIG_DIR ${libusb_SOURCE_DIR}/android)

add_library(usb-1.0 STATIC
    ${libusb_SOURCE_DIR}/libusb/core.c
    ${libusb_SOURCE_DIR}/libusb/descriptor.c
    ${libusb_SOURCE_DIR}/libusb/hotplug.c
    ${libusb_SOURCE_DIR}/libusb/io.c
    ${libusb_SOURCE_DIR}/libusb/strerror.c
    ${libusb_SOURCE_DIR}/libusb/sync.c
    ${libusb_SOURCE_DIR}/libusb/os/linux_usbfs.c
    ${libusb_SOURCE_DIR}/libusb/os/events_posix.c
    ${libusb_SOURCE_DIR}/libusb/os/threads_posix.c
)
target_include_directories(usb-1.0 PUBLIC
    ${libusb_SOURCE_DIR}/libusb
    ${libusb_SOURCE_DIR}/libusb/os
    ${LIBUSB_CONFIG_DIR}
)
target_compile_definitions(usb-1.0 PRIVATE
    LIBUSB_API_VERSION=0x0100010A
    PLATFORM_POSIX
)
target_link_libraries(usb-1.0 log)

# ── libuvc (saki4510t fork) ──────────────────────────────────────────────────
FetchContent_Declare(
    libuvc
    GIT_REPOSITORY https://github.com/saki4510t/libuvc.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(libuvc)
if(NOT libuvc_POPULATED)
    FetchContent_Populate(libuvc)
endif()

set(LIBUVC_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/libuvc_config)
file(MAKE_DIRECTORY ${LIBUVC_CONFIG_DIR}/libuvc)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/libuvc_config.h
     DESTINATION ${LIBUVC_CONFIG_DIR}/libuvc)

set(LIBUVC_SOURCES
    ${libuvc_SOURCE_DIR}/src/ctrl.c
    ${libuvc_SOURCE_DIR}/src/ctrl-gen.c
    ${libuvc_SOURCE_DIR}/src/device.c
    ${libuvc_SOURCE_DIR}/src/diag.c
    ${libuvc_SOURCE_DIR}/src/frame.c
    ${libuvc_SOURCE_DIR}/src/init.c
    ${libuvc_SOURCE_DIR}/src/stream.c
    ${libuvc_SOURCE_DIR}/src/misc.c
)

add_library(uvc_static STATIC ${LIBUVC_SOURCES})
target_include_directories(uvc_static PUBLIC
    ${libuvc_SOURCE_DIR}/include
    ${LIBUVC_CONFIG_DIR}
)
target_compile_definitions(uvc_static PRIVATE LIBUVC_HAS_JPEG=0)
target_link_libraries(uvc_static usb-1.0 log)

# ── Main JNI library ────────────────────────────────────────────────────────
add_library(neximage_probe SHARED
    neximage_probe.cpp
    uvc_capture.cpp
    gl_preview.cpp
    pixel_format.cpp
    android_uvc_fd.c
)

target_include_directories(neximage_probe PRIVATE
    ${libuvc_SOURCE_DIR}/include
    ${libusb_SOURCE_DIR}/libusb
    ${LIBUVC_CONFIG_DIR}
)

find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)

target_link_libraries(neximage_probe
    uvc_static
    usb-1.0
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
)
