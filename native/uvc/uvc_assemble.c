#include "uvc_types.h"

#include <string.h>

void uvc_assembler_init(UvcAssembler *a, unsigned char *storage, int capacity, int expected_size) {
    memset(a, 0, sizeof(*a));
    a->frame = storage;
    a->capacity = capacity;
    a->expected_size = expected_size;
    a->active_fid = -1;
}

static void drop_open_frame(UvcAssembler *a) {
    if (a->saw_data) {
        a->frames_dropped++;
    }
    a->length = 0;
    a->saw_data = 0;
    a->error = 0;
    a->has_pts = 0;
}

int uvc_assembler_push(UvcAssembler *a, const unsigned char *payload, int len) {
    int hlen;
    int info;
    int fid;
    int data_len;
    const unsigned char *data;

    a->packets++;
    if (a->held) {
        a->held = 0;
        a->length = 0;
        a->error = 0;
        a->has_pts = 0;
        a->saw_data = 0;
    }
    if (len < 2 || payload == NULL) {
        a->bad_headers++;
        return UVC_PUSH_NEED_MORE;
    }
    hlen = payload[0];
    if (hlen < 2 || hlen > len || hlen > 255) {
        a->bad_headers++;
        return UVC_PUSH_NEED_MORE;
    }
    info = payload[1];
    fid = info & 0x01;
    if (info & 0x40) {
        a->err_bits++;
        a->error = 1;
    }
    if (a->active_fid >= 0 && fid != a->active_fid) {
        if (a->saw_data) {
            a->frames_dropped++;
        }
        a->length = 0;
        a->saw_data = 0;
        a->error = 0;
        a->has_pts = 0;
    }
    a->active_fid = fid;
    if ((info & 0x04) && hlen >= 6) {
        a->has_pts = 1;
        a->pts = (uint32_t)(payload[2] | (payload[3] << 8) | (payload[4] << 16) | (payload[5] << 24));
    }

    data = payload + hlen;
    data_len = len - hlen;
    a->payload_bytes += (uint64_t)len;
    if (data_len > 0) {
        if (a->length + data_len > a->capacity) {
            drop_open_frame(a);
            a->active_fid = fid;
            return UVC_PUSH_DROPPED;
        }
        memcpy(a->frame + a->length, data, (size_t)data_len);
        a->length += data_len;
        a->saw_data = 1;
    }

    if (info & 0x02) {
        int ok = a->saw_data && !a->error;
        if (a->expected_size > 0 && a->length != a->expected_size) {
            ok = 0;
        }
        a->last_length = a->length;
        if (ok) {
            a->frames_ok++;
            a->held = 1;
            a->saw_data = 0;
            return UVC_PUSH_FRAME;
        }
        drop_open_frame(a);
        return UVC_PUSH_DROPPED;
    }
    return UVC_PUSH_NEED_MORE;
}

void uvc_assembler_take(UvcAssembler *a) {
    a->length = 0;
    a->has_pts = 0;
}
