#include "bayer_score.h"
#include "uvc_assemble.h"
#include "uvc_desc.h"
#include "uvc_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed++;
    }
}

static void test_guid(void) {
    unsigned char guid[16];
    UvcGuidInfo info;
    char text[33];
    expect(uvc_parse_guid_text("47524247-0000-1000-8000-00aa00389b71", guid) == 0, "parse user guid");
    expect(guid[0] == 'G' && guid[1] == 'R' && guid[2] == 'B' && guid[3] == 'G', "wire fourcc GRBG");
    expect(guid[6] == 0x10 && guid[7] == 0x00, "uvc marker");
    uvc_identify_guid(guid, 8, &info);
    expect(info.pixel_kind == UVC_PIXEL_BAYER8, "kind bayer8");
    expect(info.bayer_phase == UVC_BAYER_GRBG, "phase grbg");
    expect(uvc_guid_is_raw_sensor(&info), "raw sensor");
    uvc_guid_to_bytes_text(guid, text);
    expect(strcmp(text, "4752424700001000800000aa00389b71") == 0, "byte text");

    expect(uvc_parse_guid_text("47425247-0000-0010-8000-00aa00389b71", guid) == 0, "parse microsoft guid");
    expect(guid[0] == 'G' && guid[1] == 'R' && guid[2] == 'B' && guid[3] == 'G', "ms guid is GRBG");

    expect(uvc_parse_guid_text("59383030-0000-1000-8000-00aa00389b71", guid) == 0, "parse y800");
    uvc_identify_guid(guid, 8, &info);
    expect(strcmp(info.fourcc, "Y800") == 0, "y800 fourcc");
    expect(info.pixel_kind == UVC_PIXEL_MONO8, "y800 mono");
}

static void put_desc(unsigned char *d, int *n, const unsigned char *src, int len) {
    memcpy(d + *n, src, (size_t)len);
    *n += len;
}

static void test_desc(void) {
    unsigned char raw[256];
    int n = 0;
    UvcParsedDevice dev;
    unsigned char config[] = {9, 0x02, 0, 0, 1, 1, 0, 0x80, 96};
    unsigned char iface_vc[] = {9, 0x04, 0, 0, 0, 14, 1, 0, 0};
    unsigned char iface_vs[] = {9, 0x04, 1, 1, 1, 14, 2, 0, 0};
    unsigned char format[] = {
        27, 0x24, 0x04, 1, 1,
        'G', 'R', 'B', 'G', 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71,
        8, 1, 0, 0, 0, 0
    };
    unsigned char frame[38];
    unsigned char ep[] = {7, 0x05, 0x81, 0x01, 0x00, 0x14, 1};
    unsigned char ss[] = {6, 0x30, 15, 0, 0x00, 0x04};
    int alt;
    memset(frame, 0, sizeof(frame));
    frame[0] = 30;
    frame[1] = 0x24;
    frame[2] = 0x05;
    frame[3] = 1;
    frame[5] = 0x20;
    frame[6] = 0x0f;
    frame[7] = 0xcc;
    frame[8] = 0x0a;
    frame[21] = 0x40;
    frame[22] = 0x0d;
    frame[23] = 0x03;
    frame[24] = 0x00;
    frame[25] = 1;
    frame[26] = 0x40;
    frame[27] = 0x0d;
    frame[28] = 0x03;
    frame[29] = 0x00;
    put_desc(raw, &n, config, 9);
    put_desc(raw, &n, iface_vc, 9);
    put_desc(raw, &n, iface_vs, 9);
    put_desc(raw, &n, format, 27);
    put_desc(raw, &n, frame, 30);
    put_desc(raw, &n, ep, 7);
    put_desc(raw, &n, ss, 6);
    expect(uvc_parse_config(raw, n, 0x199e, 0x8619, &dev) == 1, "one format");
    expect(dev.vendor == 0x199e, "vid");
    expect(dev.max_power_units == 96, "power units");
    expect(dev.max_power_ma_usb3 == 768, "usb3 ma");
    expect(dev.video_control_interface == 0, "vc");
    expect(dev.video_stream_interface == 1, "vs");
    expect(dev.formats[0].width == 3872, "width");
    expect(dev.formats[0].height == 2764, "height");
    expect(dev.formats[0].info.bayer_phase == UVC_BAYER_GRBG, "parsed phase");
    expect(dev.formats[0].default_interval == 200000, "interval");
    expect(dev.alt_count == 1, "one alt");
    expect(dev.alts[0].ep.is_isochronous == 1, "iso");
    expect(dev.alts[0].ep.max_packet == 1024, "packet");
    expect(dev.alts[0].ep.max_burst == 15, "burst");
    alt = uvc_choose_alt(&dev, 1024);
    expect(alt == 1, "choose alt 1");
    expect(dev.saw_uncompressed == 1, "uncompressed seen");
    expect(dev.saw_frame_based == 0, "not frame based");
}

static void test_frame_based(void) {
    unsigned char raw[128];
    int n = 0;
    UvcParsedDevice dev;
    unsigned char config[] = {9, 0x02, 0, 0, 1, 1, 0, 0x80, 50};
    unsigned char iface_vs[] = {9, 0x04, 1, 0, 1, 14, 2, 0, 0};
    unsigned char format[28];
    unsigned char frame[30];
    memset(format, 0, sizeof(format));
    format[0] = 28;
    format[1] = 0x24;
    format[2] = 0x10;
    format[3] = 1;
    format[4] = 1;
    format[5] = 'G';
    format[6] = 'R';
    format[7] = 'B';
    format[8] = 'G';
    format[11] = 0x10;
    format[13] = 0x80;
    format[16] = 0xaa;
    format[18] = 0x38;
    format[19] = 0x9b;
    format[20] = 0x71;
    format[21] = 8;
    memset(frame, 0, sizeof(frame));
    frame[0] = 30;
    frame[1] = 0x24;
    frame[2] = 0x11;
    frame[3] = 1;
    frame[5] = 0x20;
    frame[6] = 0x0f;
    frame[7] = 0xcc;
    frame[8] = 0x0a;
    frame[17] = 0x40;
    frame[18] = 0x0d;
    frame[19] = 0x03;
    frame[21] = 1;
    frame[26] = 0x40;
    frame[27] = 0x0d;
    frame[28] = 0x03;
    put_desc(raw, &n, config, 9);
    put_desc(raw, &n, iface_vs, 9);
    put_desc(raw, &n, format, 28);
    put_desc(raw, &n, frame, 30);
    expect(uvc_parse_config(raw, n, 0x199e, 0x8619, &dev) == 1, "frame-based format");
    expect(dev.max_power_ma_usb2 == 100, "usb2 100 mA");
    expect(dev.max_power_ma_usb3 == 400, "usb3 400 mA");
    expect(dev.saw_frame_based == 1, "frame based flag");
    expect(dev.formats[0].width == 3872, "frame-based width");
    expect(dev.formats[0].height == 2764, "frame-based height");
    expect(dev.formats[0].info.bayer_phase == UVC_BAYER_GRBG, "frame-based phase");
    expect(dev.formats[0].default_interval == 200000, "frame-based interval");
    expect(dev.formats[0].max_frame_bytes == 3872u * 2764u, "frame-based size");
}

static void push_chunk(UvcAssembler *a, int fid, int eof, int err, const unsigned char *data, int n) {
    unsigned char payload[64];
    payload[0] = 12;
    payload[1] = (unsigned char)((fid & 1) | (eof ? 0x02 : 0) | 0x04 | (err ? 0x40 : 0));
    payload[2] = 10;
    payload[3] = 0;
    payload[4] = 0;
    payload[5] = 0;
    memset(payload + 6, 0, 6);
    memcpy(payload + 12, data, (size_t)n);
    uvc_assembler_push(a, payload, 12 + n);
}

static void test_assemble(void) {
    unsigned char storage[16];
    unsigned char a_bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    UvcAssembler a;
    int result;
    uvc_assembler_init(&a, storage, 16, 8);
    push_chunk(&a, 0, 0, 0, a_bytes, 4);
    result = UVC_PUSH_NEED_MORE;
    expect(a.length == 4, "partial length");
    result = uvc_assembler_push(&a, NULL, 0);
    (void)result;
    {
        unsigned char payload[16];
        payload[0] = 2;
        payload[1] = 0x02;
        memcpy(payload + 2, a_bytes + 4, 4);
        result = uvc_assembler_push(&a, payload, 6);
    }
    expect(result == UVC_PUSH_FRAME, "frame done");
    expect(a.length == 8, "full length");
    expect(memcmp(storage, a_bytes, 8) == 0, "bytes");
    expect(a.frames_ok == 1, "ok count");
    {
        unsigned char payload[8];
        unsigned char extra[4] = {9, 9, 9, 9};
        payload[0] = 2;
        payload[1] = 0x01;
        memcpy(payload + 2, extra, 4);
        result = uvc_assembler_push(&a, payload, 6);
        expect(result == UVC_PUSH_NEED_MORE, "next frame started");
        expect(a.length == 4, "old frame released");
    }
    {
        unsigned char payload[4];
        payload[0] = 2;
        payload[1] = 0x42;
        payload[2] = 1;
        payload[3] = 2;
        uvc_assembler_init(&a, storage, 16, 4);
        result = uvc_assembler_push(&a, payload, 4);
        expect(result == UVC_PUSH_DROPPED, "err bit drops");
        expect(a.err_bits == 1, "err counted");
    }
    {
        unsigned char payload[6] = {2, 0x02, 1, 2, 3, 4};
        uvc_assembler_init(&a, storage, 16, 8);
        result = uvc_assembler_push(&a, payload, 6);
        expect(result == UVC_PUSH_DROPPED, "short frame");
        expect(a.frames_dropped >= 1, "drop count");
        expect(a.last_length == 4, "reported short length");
    }
}

static void test_bayer(void) {
    enum { W = 32, H = 32 };
    unsigned char mosaic[W * H];
    unsigned char flat[W * H];
    unsigned char packed[W * H * 2];
    BayerScore score;
    int y;
    int x;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int v;
            if ((y & 1) == 0) v = (x & 1) == 0 ? 20 : 200;
            else v = (x & 1) == 0 ? 40 : 20;
            mosaic[y * W + x] = (unsigned char)v;
            flat[y * W + x] = 30;
            packed[(y * W + x) * 2] = 0;
            packed[(y * W + x) * 2 + 1] = (unsigned char)v;
        }
    }
    score = bayer_score_8(mosaic, W, H);
    expect(score.likely_mosaic == 1, "mosaic detected");
    expect(score.ratio > 2.0, "mosaic ratio");
    score = bayer_score_8(flat, W, H);
    expect(score.likely_mosaic == 0, "flat is not mosaic");
    score = bayer_score_16_low_byte(packed, W, H);
    expect(score.low_byte_nonzero == 0, "msb aligned");
    expect(score.likely_mosaic == 1, "16-bit high byte still mosaic");
}

int main(void) {
    test_guid();
    test_desc();
    test_frame_based();
    test_assemble();
    test_bayer();
    if (g_failed) {
        fprintf(stderr, "%d failed\n", g_failed);
        return 1;
    }
    printf("uvc tests passed\n");
    return 0;
}
