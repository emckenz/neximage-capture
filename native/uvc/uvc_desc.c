#include "uvc_types.h"
#include "uvc_format.h"

#include <string.h>

static unsigned short ru16(const unsigned char *p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned int ru32(const unsigned char *p) {
    return (unsigned int)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static int endpoint_bytes(const UvcEndpointDesc *ep) {
    int burst = ep->max_burst + 1;
    int mult = ep->mult + 1;
    if (burst < 1) burst = 1;
    if (mult < 1) mult = 1;
    return ep->max_packet * ep->transactions * burst * mult;
}

int uvc_endpoint_bandwidth(const UvcEndpointDesc *ep) {
    return endpoint_bytes(ep);
}

static void clear_ep(UvcEndpointDesc *ep) {
    memset(ep, 0, sizeof(*ep));
    ep->transactions = 1;
    ep->max_burst = 0;
    ep->mult = 0;
}

int uvc_parse_config(const unsigned char *desc, int length, int vendor, int product, UvcParsedDevice *out) {
    int offset = 0;
    int current_if = -1;
    int current_alt = 0;
    int current_class = 0;
    int current_subclass = 0;
    int in_vs = 0;
    int format_index = 0;
    int bits = 0;
    unsigned char guid[UVC_GUID_BYTES];
    int max_power = 0;
    UvcEndpointDesc pending;
    int have_pending = 0;

    memset(out, 0, sizeof(*out));
    memset(guid, 0, sizeof(guid));
    clear_ep(&pending);
    out->vendor = vendor;
    out->product = product;
    out->video_control_interface = -1;
    out->video_stream_interface = -1;

    while (offset + 2 <= length) {
        int len = desc[offset];
        int type = desc[offset + 1];
        const unsigned char *b;
        if (len < 2 || offset + len > length) break;
        b = desc + offset;

        if (type == 0x02 && len >= 9) {
            max_power = b[8];
        } else if (type == 0x04 && len >= 8) {
            have_pending = 0;
            current_if = b[2];
            current_alt = b[3];
            current_class = b[5];
            current_subclass = b[6];
            in_vs = (current_class == 14 && current_subclass == 2);
            if (current_class == 14 && current_subclass == 1 && out->video_control_interface < 0) {
                out->video_control_interface = current_if;
            }
            if (in_vs && out->video_stream_interface < 0) {
                out->video_stream_interface = current_if;
            }
            if (in_vs && out->alt_count < 16) {
                UvcAltDesc *alt = &out->alts[out->alt_count++];
                memset(alt, 0, sizeof(*alt));
                alt->alt = current_alt;
            }
        } else if (type == 0x24 && current_class == 14 && current_subclass == 1 && len >= 5) {
            int subtype = b[2];
            if (subtype == 0x02 && len >= 8 && ru16(b + 4) == 0x0201) {
                out->camera_terminal_id = b[3];
            } else if (subtype == 0x05) {
                out->processing_unit_id = b[3];
            }
        } else if (type == 0x24 && in_vs && len >= 4) {
            int subtype = b[2];
            if (subtype == 0x04 && len >= 27 && out->format_count < 32) {
                format_index = b[3];
                bits = b[21];
                memcpy(guid, b + 5, UVC_GUID_BYTES);
            } else if (subtype == 0x05 && len >= 26 && format_index > 0 && out->format_count < 32) {
                UvcFrameDesc *frame = &out->formats[out->format_count++];
                int nint;
                int i;
                memset(frame, 0, sizeof(*frame));
                frame->format_index = format_index;
                frame->frame_index = b[3];
                frame->width = ru16(b + 5);
                frame->height = ru16(b + 7);
                frame->bits_per_pixel = bits;
                frame->max_frame_bytes = ru32(b + 17);
                frame->default_interval = (int)ru32(b + 21);
                memcpy(frame->guid, guid, UVC_GUID_BYTES);
                uvc_identify_guid(guid, bits, &frame->info);
                nint = b[25];
                if (nint == 0) nint = 0;
                if (nint > 8) nint = 8;
                frame->interval_count = 0;
                for (i = 0; i < nint && 26 + (i + 1) * 4 <= len; i++) {
                    frame->intervals[frame->interval_count++] = (int)ru32(b + 26 + i * 4);
                }
                if (frame->interval_count == 0) {
                    frame->intervals[0] = frame->default_interval;
                    frame->interval_count = 1;
                }
            }
        } else if (type == 0x05 && len >= 7 && in_vs) {
            int attr = b[3] & 0x03;
            int raw = ru16(b + 4);
            clear_ep(&pending);
            pending.address = b[2];
            pending.is_isochronous = (attr == 0x01);
            if (pending.is_isochronous) {
                pending.max_packet = raw & 0x07ff;
                pending.transactions = ((raw >> 11) & 0x03) + 1;
            } else {
                pending.max_packet = raw & 0x07ff;
                pending.transactions = 1;
            }
            if ((b[2] & 0x80) != 0 && attr != 0x03) {
                have_pending = 1;
            }
        } else if (type == 0x30 && len >= 6 && have_pending) {
            pending.max_burst = b[2];
            pending.mult = b[3] & 0x03;
            pending.bytes_per_interval = ru16(b + 4);
        }

        if (have_pending && out->alt_count > 0) {
            UvcAltDesc *alt = &out->alts[out->alt_count - 1];
            if (alt->alt == current_alt) {
                alt->ep = pending;
                alt->has_ep = 1;
            }
        }
        offset += len;
    }

    out->max_power_units = max_power;
    out->max_power_ma_usb2 = max_power * 2;
    out->max_power_ma_usb3 = max_power * 8;
    return out->format_count;
}

int uvc_choose_alt(const UvcParsedDevice *dev, unsigned int payload_size) {
    int best = -1;
    int best_bytes = 0x7fffffff;
    int largest = -1;
    int largest_bytes = -1;
    int i;
    for (i = 0; i < dev->alt_count; i++) {
        int bytes;
        if (!dev->alts[i].has_ep) continue;
        bytes = endpoint_bytes(&dev->alts[i].ep);
        if (bytes > largest_bytes) {
            largest_bytes = bytes;
            largest = dev->alts[i].alt;
        }
        if ((unsigned int)bytes >= payload_size && bytes < best_bytes) {
            best_bytes = bytes;
            best = dev->alts[i].alt;
        }
    }
    if (best >= 0) return best;
    return largest;
}
