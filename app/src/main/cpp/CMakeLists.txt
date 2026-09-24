cmake_minimum_required(VERSION 3.22.1)
project(neximage_probe)

add_library(neximage SHARED
    uvc_pump.c
    ../../../../native/uvc/uvc_format.c
    ../../../../native/uvc/uvc_desc.c
    ../../../../native/uvc/uvc_assemble.c
    ../../../../native/uvc/bayer_score.c
)

target_include_directories(neximage PRIVATE ../../../../native/uvc)

target_link_libraries(neximage log)
