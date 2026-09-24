#pragma once

#include "libuvc/libuvc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Android-specific: open UVC device from UsbManager file descriptor. */
uvc_error_t uvc_get_device_with_fd(
    uvc_context_t* ctx,
    uvc_device_t** device,
    int vid,
    int pid,
    const char* serial,
    int fd,
    int busnum,
    int devaddr);

#ifdef __cplusplus
}
#endif
