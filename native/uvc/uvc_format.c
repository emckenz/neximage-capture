#include "uvc_types.h"

#include <string.h>

static void set_info(UvcGuidInfo *info, const char *fcc, int kind, int phase, int bpp) {
    memset(info, 0, sizeof(*info));
    memcpy(info->fourcc, fcc, 4);
    info->fourcc[4] = 0;
    info->pixel_kind = kind;
    info->bayer_phase = phase;
    info->bits_per_pixel = bpp;
}

void uvc_identify_guid(const unsigned char guid[UVC_GUID_BYTES], int declared_bpp, UvcGuidInfo *info) {
    char fcc[5];
    int i;
    memcpy(fcc, guid, 4);
    fcc[4] = 0;
    for (i = 0; i < 4; i++) {
        if (fcc[i] < 32 || fcc[i] > 126) {
            fcc[i] = '?';
        }
    }

    if (memcmp(fcc, "GRBG", 4) == 0) {
        set_info(info, "GRBG", UVC_PIXEL_BAYER8, UVC_BAYER_GRBG, 8);
    } else if (memcmp(fcc, "GBRG", 4) == 0) {
        set_info(info, "GBRG", UVC_PIXEL_BAYER8, UVC_BAYER_GBRG, 8);
    } else if (memcmp(fcc, "RGGB", 4) == 0) {
        set_info(info, "RGGB", UVC_PIXEL_BAYER8, UVC_BAYER_RGGB, 8);
    } else if (memcmp(fcc, "BA81", 4) == 0 || memcmp(fcc, "BY8 ", 4) == 0) {
        set_info(info, fcc, UVC_PIXEL_BAYER8, UVC_BAYER_BGGR, 8);
    } else if (memcmp(fcc, "Y800", 4) == 0 || memcmp(fcc, "GREY", 4) == 0 ||
               memcmp(fcc, "Y8  ", 4) == 0) {
        set_info(info, fcc, UVC_PIXEL_MONO8, UVC_BAYER_NONE, 8);
    } else if (memcmp(fcc, "GR16", 4) == 0) {
        set_info(info, "GR16", UVC_PIXEL_BAYER16, UVC_BAYER_GRBG, 16);
    } else if (memcmp(fcc, "GB16", 4) == 0) {
        set_info(info, "GB16", UVC_PIXEL_BAYER16, UVC_BAYER_GBRG, 16);
    } else if (memcmp(fcc, "RG16", 4) == 0) {
        set_info(info, "RG16", UVC_PIXEL_BAYER16, UVC_BAYER_RGGB, 16);
    } else if (memcmp(fcc, "BA16", 4) == 0 || memcmp(fcc, "BYR2", 4) == 0) {
        set_info(info, fcc, UVC_PIXEL_BAYER16, UVC_BAYER_BGGR, 16);
    } else if (memcmp(fcc, "Y16 ", 4) == 0) {
        set_info(info, "Y16 ", UVC_PIXEL_MONO16, UVC_BAYER_NONE, 16);
    } else if (memcmp(fcc, "Y12p", 4) == 0 || memcmp(fcc, "GRCp", 4) == 0) {
        set_info(info, fcc, UVC_PIXEL_PACKED12, memcmp(fcc, "GRCp", 4) == 0 ? UVC_BAYER_GRBG : UVC_BAYER_NONE, 12);
    } else if (memcmp(fcc, "YUY2", 4) == 0 || memcmp(fcc, "YUYV", 4) == 0) {
        set_info(info, "YUY2", UVC_PIXEL_YUYV, UVC_BAYER_NONE, 16);
    } else if (memcmp(fcc, "RGBP", 4) == 0) {
        set_info(info, "RGBP", UVC_PIXEL_RGB, UVC_BAYER_NONE, 16);
    } else if (memcmp(fcc, "MJPG", 4) == 0) {
        set_info(info, "MJPG", UVC_PIXEL_COMPRESSED, UVC_BAYER_NONE, declared_bpp > 0 ? declared_bpp : 0);
    } else {
        set_info(info, fcc, UVC_PIXEL_UNKNOWN, UVC_BAYER_NONE, declared_bpp);
    }
}

int uvc_guid_is_raw_sensor(const UvcGuidInfo *info) {
    return info->pixel_kind == UVC_PIXEL_BAYER8 ||
           info->pixel_kind == UVC_PIXEL_MONO8 ||
           info->pixel_kind == UVC_PIXEL_BAYER16 ||
           info->pixel_kind == UVC_PIXEL_MONO16 ||
           info->pixel_kind == UVC_PIXEL_PACKED12;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int marker_ok(const unsigned char guid[UVC_GUID_BYTES]) {
    return guid[4] == 0x00 && guid[5] == 0x00 && guid[6] == 0x10 && guid[7] == 0x00 &&
           guid[8] == 0x80 && guid[9] == 0x00 && guid[10] == 0x00 && guid[11] == 0xaa;
}

static void guid_from_microsoft(unsigned char guid[UVC_GUID_BYTES]) {
    unsigned char t;
    t = guid[0]; guid[0] = guid[3]; guid[3] = t;
    t = guid[1]; guid[1] = guid[2]; guid[2] = t;
    t = guid[4]; guid[4] = guid[5]; guid[5] = t;
    t = guid[6]; guid[6] = guid[7]; guid[7] = t;
}

int uvc_parse_guid_text(const char *text, unsigned char guid[UVC_GUID_BYTES]) {
    char compact[33];
    int n = 0;
    int i;
    if (!text) return -1;
    for (i = 0; text[i] != 0 && n < 32; i++) {
        if (text[i] == '-' || text[i] == ' ' || text[i] == '{' || text[i] == '}') continue;
        compact[n++] = text[i];
    }
    if (n != 32) return -1;
    for (i = 0; i < 16; i++) {
        int hi = hex_nibble(compact[i * 2]);
        int lo = hex_nibble(compact[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        guid[i] = (unsigned char)((hi << 4) | lo);
    }
    if (!marker_ok(guid)) {
        guid_from_microsoft(guid);
    }
    return 0;
}

void uvc_guid_to_bytes_text(const unsigned char guid[UVC_GUID_BYTES], char out[33]) {
    static const char *kHex = "0123456789abcdef";
    int i;
    for (i = 0; i < 16; i++) {
        out[i * 2] = kHex[guid[i] >> 4];
        out[i * 2 + 1] = kHex[guid[i] & 0x0f];
    }
    out[32] = 0;
}
