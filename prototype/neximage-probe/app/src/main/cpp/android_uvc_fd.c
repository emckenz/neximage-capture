/**
 * Android USB fd → libuvc bridge.
 * Based on saki4510t/UVCCamera libuvc patches (Apache-2.0).
 */
#include "android_uvc_fd.h"

#include "libuvc/libuvc_internal.h"
#include <stdlib.h>
#include <string.h>

uvc_error_t uvc_get_device_with_fd(
    uvc_context_t* ctx,
    uvc_device_t** device,
    int vid,
    int pid,
    const char* /*serial*/,
    int fd,
    int busnum,
    int devaddr) {

    if (!ctx || !device || fd < 0) return UVC_ERROR_INVALID_PARAM;

    libusb_device_handle* handle = NULL;
    int r = libusb_wrap_sys_device(ctx->usb_ctx, (intptr_t)fd, &handle);
    if (r != LIBUSB_SUCCESS || !handle) {
        return UVC_ERROR_IO;
    }

    libusb_device* usb_dev = libusb_get_device(handle);
    if (!usb_dev) {
        libusb_close(handle);
        return UVC_ERROR_NO_DEVICE;
    }

    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(usb_dev, &desc) != LIBUSB_SUCCESS) {
        libusb_close(handle);
        return UVC_ERROR_IO;
    }

    if (vid > 0 && desc.idVendor != (uint16_t)vid) {
        libusb_close(handle);
        return UVC_ERROR_NO_DEVICE;
    }
    if (pid > 0 && desc.idProduct != (uint16_t)pid) {
        libusb_close(handle);
        return UVC_ERROR_NO_DEVICE;
    }

    (void)busnum;
    (void)devaddr;

    uvc_device_t* dev = calloc(1, sizeof(uvc_device_t));
    if (!dev) {
        libusb_close(handle);
        return UVC_ERROR_NO_MEM;
    }

    dev->ctx = ctx;
    dev->usb_dev = usb_dev;
    dev->ref = 0;
    uvc_ref_device(dev);

    libusb_set_auto_detach_kernel_driver(handle, 1);
    /* Keep handle open — uvc_open will create its own handle */
    libusb_close(handle);

    *device = dev;
    return UVC_SUCCESS;
}
