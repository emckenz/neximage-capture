#ifndef NEXIMAGE_UVC_TYPES_H
#define NEXIMAGE_UVC_TYPES_H

#include <stdint.h>

#define UVC_GUID_BYTES 16

enum UvcPixelKind {
    UVC_PIXEL_UNKNOWN = 0,
    UVC_PIXEL_BAYER8,
    UVC_PIXEL_MONO8,
    UVC_PIXEL_BAYER16,
    UVC_PIXEL_MONO16,
    UVC_PIXEL_PACKED12,
    UVC_PIXEL_YUYV,
    UVC_PIXEL_RGB,
    UVC_PIXEL_COMPRESSED
};

enum UvcBayerPhase {
    UVC_BAYER_NONE = 0,
    UVC_BAYER_GRBG,
    UVC_BAYER_GBRG,
    UVC_BAYER_RGGB,
    UVC_BAYER_BGGR
};

typedef struct UvcGuidInfo {
    char fourcc[5];
    int pixel_kind;
    int bayer_phase;
    int bits_per_pixel;
} UvcGuidInfo;

typedef struct UvcFrameDesc {
    int format_index;
    int frame_index;
    int width;
    int height;
    int bits_per_pixel;
    int default_interval;
    int interval_count;
    int intervals[8];
    unsigned char guid[UVC_GUID_BYTES];
    UvcGuidInfo info;
    unsigned int max_frame_bytes;
} UvcFrameDesc;

typedef struct UvcEndpointDesc {
    int address;
    int is_isochronous;
    int max_packet;
    int transactions;
    int max_burst;
    int mult;
    int bytes_per_interval;
} UvcEndpointDesc;

typedef struct UvcAltDesc {
    int alt;
    UvcEndpointDesc ep;
    int has_ep;
} UvcAltDesc;

typedef struct UvcParsedDevice {
    int vendor;
    int product;
    int max_power_units;
    int max_power_ma_usb2;
    int max_power_ma_usb3;
    int video_control_interface;
    int video_stream_interface;
    int camera_terminal_id;
    int processing_unit_id;
    int format_count;
    UvcFrameDesc formats[32];
    int alt_count;
    UvcAltDesc alts[16];
} UvcParsedDevice;

typedef struct UvcAssembler {
    unsigned char *frame;
    int capacity;
    int length;
    int active_fid;
    int saw_data;
    int error;
    int has_pts;
    uint32_t pts;
    int expected_size;
    uint64_t packets;
    uint64_t payload_bytes;
    uint64_t frames_ok;
    uint64_t frames_dropped;
    uint64_t err_bits;
    uint64_t bad_headers;
    int held;
    int last_length;
} UvcAssembler;

enum UvcPushResult {
    UVC_PUSH_NEED_MORE = 0,
    UVC_PUSH_FRAME = 1,
    UVC_PUSH_DROPPED = 2
};

#endif
