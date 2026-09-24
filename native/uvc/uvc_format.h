#ifndef NEXIMAGE_UVC_FORMAT_H
#define NEXIMAGE_UVC_FORMAT_H

#include "uvc_types.h"

void uvc_identify_guid(const unsigned char guid[UVC_GUID_BYTES], int declared_bpp, UvcGuidInfo *info);
int uvc_guid_is_raw_sensor(const UvcGuidInfo *info);
int uvc_parse_guid_text(const char *text, unsigned char guid[UVC_GUID_BYTES]);
void uvc_guid_to_bytes_text(const unsigned char guid[UVC_GUID_BYTES], char out[33]);

#endif
