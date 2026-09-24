#include "bayer_score.h"
#include "uvc_assemble.h"
#include "uvc_desc.h"
#include "uvc_format.h"

#include <android/log.h>
#include <errno.h>
#include <jni.h>
#include <linux/usbdevice_fs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "neximage", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "neximage", __VA_ARGS__)

static pthread_t g_thread;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_started = 0;
static volatile int g_run = 0;
static int g_fd = -1;
static struct UrbSlot *g_slots = NULL;
static int g_slot_count = 0;

static unsigned char *g_storage = NULL;
static unsigned char *g_published = NULL;
static int g_capacity = 0;
static int g_published_len = 0;
static int g_width = 0;
static int g_height = 0;
static UvcAssembler g_asm;
static uint64_t g_seq = 0;
static uint64_t g_urb_errors = 0;
static int g_speed = -1;
static int g_ratio_milli = 0;
static int g_mosaic = 0;

struct UrbSlot {
    struct usbdevfs_urb *urb;
    unsigned char *buffer;
    int iso;
};

static int set_alt(int fd, int interface_number, int alt) {
    struct usbdevfs_setinterface req;
    req.interface = (unsigned int)interface_number;
    req.altsetting = (unsigned int)alt;
    if (ioctl(fd, USBDEVFS_SETINTERFACE, &req) < 0) {
        return -errno;
    }
    return 0;
}

static int claim_if(int fd, int interface_number) {
    unsigned int ifno = (unsigned int)interface_number;
    if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &ifno) < 0) {
        if (errno == EBUSY) return 0;
        return -errno;
    }
    return 0;
}

static void publish_frame(void) {
    BayerScore score;
    pthread_mutex_lock(&g_mu);
    if (g_asm.length > 0 && g_asm.length <= g_capacity) {
        memcpy(g_published, g_asm.frame, (size_t)g_asm.length);
        g_published_len = g_asm.length;
        g_seq++;
        if ((g_seq % 12u) == 0u && g_width >= 8 && g_height >= 8 &&
            g_asm.length >= g_width * g_height) {
            score = bayer_score_8(g_published, g_width, g_height);
            g_ratio_milli = (int)(score.ratio * 1000.0);
            g_mosaic = score.likely_mosaic;
        }
    }
    pthread_mutex_unlock(&g_mu);
}

static void consume_payload(const unsigned char *data, int len) {
    int result;
    if (len <= 0) return;
    result = uvc_assembler_push(&g_asm, data, len);
    if (result == UVC_PUSH_FRAME) {
        publish_frame();
    }
}

static void handle_urb(struct usbdevfs_urb *urb) {
    if (urb->status != 0 && urb->status != -EREMOTEIO) {
        g_urb_errors++;
    }
    if (urb->type == USBDEVFS_URB_TYPE_ISO) {
        int offset = 0;
        int i;
        for (i = 0; i < urb->number_of_packets; i++) {
            int n = (int)urb->iso_frame_desc[i].actual_length;
            if (urb->iso_frame_desc[i].status == 0 && n > 0) {
                consume_payload(urb->buffer + offset, n);
            } else if (urb->iso_frame_desc[i].status != 0 &&
                       urb->iso_frame_desc[i].status != -EREMOTEIO) {
                g_urb_errors++;
            }
            offset += (int)urb->iso_frame_desc[i].length;
        }
    } else if (urb->actual_length > 0) {
        consume_payload(urb->buffer, urb->actual_length);
    }
}

static void *pump_main(void *arg) {
    (void)arg;
    while (g_run) {
        struct usbdevfs_urb *done = NULL;
        int rc = ioctl(g_fd, USBDEVFS_REAPURB, &done);
        if (rc < 0) {
            if (errno == EINTR) continue;
            if (!g_run) break;
            g_urb_errors++;
            usleep(1000);
            continue;
        }
        if (done == NULL) continue;
        handle_urb(done);
        if (!g_run) break;
        done->status = 0;
        if (ioctl(g_fd, USBDEVFS_SUBMITURB, done) < 0) {
            g_urb_errors++;
            LOGE("resubmit failed errno=%d", errno);
            break;
        }
    }
    return NULL;
}

static struct usbdevfs_urb *make_urb(int iso, int endpoint, int packet_size, int packets) {
    int desc_bytes = iso ? packets * (int)sizeof(struct usbdevfs_iso_packet_desc) : 0;
    struct usbdevfs_urb *urb = (struct usbdevfs_urb *)calloc(1, sizeof(*urb) + (size_t)desc_bytes);
    int bytes = iso ? packet_size * packets : packet_size;
    int i;
    if (!urb) return NULL;
    urb->type = iso ? USBDEVFS_URB_TYPE_ISO : USBDEVFS_URB_TYPE_BULK;
    urb->endpoint = (unsigned char)endpoint;
    urb->flags = USBDEVFS_URB_ISO_ASAP;
    urb->buffer = malloc((size_t)bytes);
    urb->buffer_length = bytes;
    urb->usercontext = urb;
    if (!urb->buffer) {
        free(urb);
        return NULL;
    }
    if (iso) {
        urb->number_of_packets = packets;
        for (i = 0; i < packets; i++) {
            urb->iso_frame_desc[i].length = (unsigned int)packet_size;
        }
    }
    return urb;
}

static void free_slots(struct UrbSlot *slots, int count) {
    int i;
    if (!slots) return;
    for (i = 0; i < count; i++) {
        if (!slots[i].urb) continue;
        ioctl(g_fd, USBDEVFS_DISCARDURB, slots[i].urb);
        free(slots[i].urb->buffer);
        free(slots[i].urb);
    }
    free(slots);
}

JNIEXPORT jstring JNICALL
Java_astro_neximage_probe_NativePump_parseConfig(JNIEnv *env, jobject, jbyteArray raw, jint vendor, jint product) {
    jsize n = (*env)->GetArrayLength(env, raw);
    jbyte *bytes = (*env)->GetByteArrayElements(env, raw, NULL);
    UvcParsedDevice dev;
    char *json;
    size_t cap = 8192;
    size_t used = 0;
    int i;
    jstring out;
    if (!bytes) return (*env)->NewStringUTF(env, "{}");
    uvc_parse_config((const unsigned char *)bytes, (int)n, vendor, product, &dev);
    (*env)->ReleaseByteArrayElements(env, raw, bytes, JNI_ABORT);
    json = (char *)malloc(cap);
    if (!json) return (*env)->NewStringUTF(env, "{}");
    used += (size_t)snprintf(json + used, cap - used,
                             "{\"powerUnits\":%d,\"usb2mA\":%d,\"usb3mA\":%d,\"vc\":%d,\"vs\":%d,"
                             "\"cameraTerminal\":%d,\"processingUnit\":%d,\"formats\":[",
                             dev.max_power_units, dev.max_power_ma_usb2, dev.max_power_ma_usb3,
                             dev.video_control_interface, dev.video_stream_interface,
                             dev.camera_terminal_id, dev.processing_unit_id);
    for (i = 0; i < dev.format_count; i++) {
        const UvcFrameDesc *f = &dev.formats[i];
        char guid[33];
        char fcc[5];
        int c;
        uvc_guid_to_bytes_text(f->guid, guid);
        for (c = 0; c < 4; c++) {
            char ch = f->info.fourcc[c];
            fcc[c] = (ch >= 33 && ch <= 126) ? ch : '_';
        }
        fcc[4] = 0;
        if (used + 512 > cap) break;
        used += (size_t)snprintf(json + used, cap - used,
                                 "%s{\"fcc\":\"%s\",\"guid\":\"%s\",\"w\":%d,\"h\":%d,\"bpp\":%d,"
                                 "\"kind\":%d,\"phase\":%d,\"interval\":%d,\"format\":%d,\"frame\":%d,"
                                 "\"maxBytes\":%u}",
                                 i ? "," : "", fcc, guid, f->width, f->height,
                                 f->bits_per_pixel, f->info.pixel_kind, f->info.bayer_phase,
                                 f->default_interval, f->format_index, f->frame_index,
                                 f->max_frame_bytes);
    }
    used += (size_t)snprintf(json + used, cap - used, "],\"alts\":[");
    for (i = 0; i < dev.alt_count; i++) {
        const UvcAltDesc *a = &dev.alts[i];
        if (used + 256 > cap) break;
        used += (size_t)snprintf(json + used, cap - used,
                                 "%s{\"alt\":%d,\"hasEp\":%d,\"addr\":%d,\"iso\":%d,\"packet\":%d,"
                                 "\"transactions\":%d,\"burst\":%d,\"mult\":%d}",
                                 i ? "," : "", a->alt, a->has_ep, a->ep.address, a->ep.is_isochronous,
                                 a->ep.max_packet, a->ep.transactions, a->ep.max_burst, a->ep.mult);
    }
    snprintf(json + used, cap - used, "]}");
    out = (*env)->NewStringUTF(env, json);
    free(json);
    return out;
}

JNIEXPORT jstring JNICALL
Java_astro_neximage_probe_NativePump_start(JNIEnv *env, jobject, jint fd, jint interface_number,
                                            jint alt, jint endpoint, jint iso, jint packet_size,
                                            jint packets_per_urb, jint urb_count, jint frame_bytes,
                                            jint width, jint height) {
    struct UrbSlot *slots;
    int i;
    int rc;
    int speed;
    char err[128];
    if (g_started) {
        return (*env)->NewStringUTF(env, "already started");
    }
    if (packet_size < 1 || packet_size > 1024 * 1024 || urb_count < 2 || urb_count > 32) {
        return (*env)->NewStringUTF(env, "bad urb parameters");
    }
    if (iso) {
        if (packets_per_urb < 1) packets_per_urb = 8;
        if (packets_per_urb > 12) packets_per_urb = 12;
    } else {
        packets_per_urb = 1;
    }
    if (frame_bytes < 4096) frame_bytes = 4096;
    if (frame_bytes > 32 * 1024 * 1024) frame_bytes = 32 * 1024 * 1024;
    g_fd = dup(fd);
    if (g_fd < 0) {
        snprintf(err, sizeof(err), "dup failed errno=%d", errno);
        return (*env)->NewStringUTF(env, err);
    }
    rc = claim_if(g_fd, interface_number);
    if (rc < 0) {
        close(g_fd);
        g_fd = -1;
        snprintf(err, sizeof(err), "claim interface %d failed (%d)", interface_number, rc);
        return (*env)->NewStringUTF(env, err);
    }
    rc = set_alt(g_fd, interface_number, alt);
    if (rc < 0) {
        close(g_fd);
        g_fd = -1;
        snprintf(err, sizeof(err), "set alt %d failed (%d)", alt, rc);
        return (*env)->NewStringUTF(env, err);
    }
    speed = ioctl(g_fd, USBDEVFS_GET_SPEED);
    g_speed = speed;
    g_capacity = frame_bytes;
    g_storage = (unsigned char *)malloc((size_t)frame_bytes);
    g_published = (unsigned char *)malloc((size_t)frame_bytes);
    if (!g_storage || !g_published) {
        free(g_storage);
        free(g_published);
        close(g_fd);
        g_fd = -1;
        return (*env)->NewStringUTF(env, "frame buffer alloc failed");
    }
    uvc_assembler_init(&g_asm, g_storage, frame_bytes, frame_bytes);
    g_width = width;
    g_height = height;
    g_published_len = 0;
    g_seq = 0;
    g_urb_errors = 0;
    g_ratio_milli = 0;
    g_mosaic = 0;
    slots = (struct UrbSlot *)calloc((size_t)urb_count, sizeof(*slots));
    if (!slots) {
        return (*env)->NewStringUTF(env, "urb alloc failed");
    }
    for (i = 0; i < urb_count; i++) {
        slots[i].urb = make_urb(iso, endpoint, packet_size, packets_per_urb);
        slots[i].iso = iso;
        if (!slots[i].urb || ioctl(g_fd, USBDEVFS_SUBMITURB, slots[i].urb) < 0) {
            int e = errno;
            free_slots(slots, i + 1);
            free(g_storage);
            free(g_published);
            g_storage = NULL;
            g_published = NULL;
            close(g_fd);
            g_fd = -1;
            snprintf(err, sizeof(err), "submit urb %d failed errno=%d", i, e);
            return (*env)->NewStringUTF(env, err);
        }
    }
    g_slots = slots;
    g_slot_count = urb_count;
    g_run = 1;
    if (pthread_create(&g_thread, NULL, pump_main, NULL) != 0) {
        g_run = 0;
        free_slots(slots, urb_count);
        g_slots = NULL;
        g_slot_count = 0;
        return (*env)->NewStringUTF(env, "thread failed");
    }
    g_started = 1;
    LOGI("stream started iso=%d packet=%d urbs=%d speed=%d", iso, packet_size, urb_count, g_speed);
    return (*env)->NewStringUTF(env, "");
}

JNIEXPORT void JNICALL
Java_astro_neximage_probe_NativePump_stop(JNIEnv *, jobject) {
    if (!g_started) return;
    g_run = 0;
    if (g_slots && g_fd >= 0) {
        int i;
        for (i = 0; i < g_slot_count; i++) {
            if (g_slots[i].urb) ioctl(g_fd, USBDEVFS_DISCARDURB, g_slots[i].urb);
        }
    }
    pthread_join(g_thread, NULL);
    free_slots(g_slots, g_slot_count);
    g_slots = NULL;
    g_slot_count = 0;
    if (g_fd >= 0) close(g_fd);
    g_fd = -1;
    free(g_storage);
    free(g_published);
    g_storage = NULL;
    g_published = NULL;
    g_started = 0;
}

JNIEXPORT jboolean JNICALL
Java_astro_neximage_probe_NativePump_poll(JNIEnv *env, jobject, jobject buffer, jlongArray stats) {
    jlong values[16];
    void *dst;
    jsize stat_len;
    int fresh = 0;
    int len = 0;
    uint64_t seq;
    uint64_t frames_ok;
    uint64_t frames_dropped;
    uint64_t packets;
    uint64_t payload;
    uint64_t err_bits;
    uint64_t bad_headers;
    uint64_t urb_errors;
    int pts;
    int ratio;
    int mosaic;
    int speed;
    pthread_mutex_lock(&g_mu);
    seq = g_seq;
    len = g_published_len;
    frames_ok = g_asm.frames_ok;
    frames_dropped = g_asm.frames_dropped;
    packets = g_asm.packets;
    payload = g_asm.payload_bytes;
    err_bits = g_asm.err_bits;
    bad_headers = g_asm.bad_headers;
    urb_errors = g_urb_errors;
    pts = (int)g_asm.pts;
    ratio = g_ratio_milli;
    mosaic = g_mosaic;
    speed = g_speed;
    dst = buffer ? (*env)->GetDirectBufferAddress(env, buffer) : NULL;
    if (dst && len > 0) {
        jlong cap = (*env)->GetDirectBufferCapacity(env, buffer);
        if (cap >= len) {
            memcpy(dst, g_published, (size_t)len);
            fresh = 1;
        }
    }
    pthread_mutex_unlock(&g_mu);
    memset(values, 0, sizeof(values));
    values[0] = (jlong)seq;
    values[1] = (jlong)frames_ok;
    values[2] = (jlong)frames_dropped;
    values[3] = (jlong)packets;
    values[4] = (jlong)payload;
    values[5] = (jlong)err_bits;
    values[6] = (jlong)bad_headers;
    values[7] = speed;
    values[8] = pts;
    values[9] = len;
    values[10] = (jlong)urb_errors;
    values[11] = ratio;
    values[12] = mosaic;
    values[13] = g_asm.last_length;
    stat_len = (*env)->GetArrayLength(env, stats);
    (*env)->SetLongArrayRegion(env, stats, 0, stat_len < 16 ? stat_len : 16, values);
    return fresh ? JNI_TRUE : JNI_FALSE;
}
