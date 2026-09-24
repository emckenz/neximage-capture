#ifndef NEXIMAGE_UVC_DESC_H
#define NEXIMAGE_UVC_DESC_H

#include "uvc_types.h"

int uvc_parse_config(const unsigned char *desc, int length, int vendor, int product, UvcParsedDevice *out);
int uvc_choose_alt(const UvcParsedDevice *dev, unsigned int payload_size);
int uvc_endpoint_bandwidth(const UvcEndpointDesc *ep);

#endif
