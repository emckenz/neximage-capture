#include "uvc_capture.h"

#include <android/log.h>
#include <chrono>
#include <cstring>
#include <sstream>

#include "android_uvc_fd.h"
#include "libuvc/libuvc.h"

#define LOG_TAG "UvcCapture"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uvc_frame_format fourccToFormat(const char* fourcc) {
    if (!fourcc) return UVC_FRAME_FORMAT_ANY;
    if (strncmp(fourcc, "Y800", 4) == 0 || strncmp(fourcc, "GREY", 4) == 0) {
        return UVC_FRAME_FORMAT_GRAY8;
    }
    if (strncmp(fourcc, "GRBG", 4) == 0) return UVC_FRAME_FORMAT_SGRBG8;
    if (strncmp(fourcc, "RGGB", 4) == 0) return UVC_FRAME_FORMAT_SRGGB8;
    if (strncmp(fourcc, "GBRG", 4) == 0) return UVC_FRAME_FORMAT_SGBRG8;
    if (strncmp(fourcc, "BGGR", 4) == 0) return UVC_FRAME_FORMAT_SBGGR8;
    if (strncmp(fourcc, "YUY2", 4) == 0) return UVC_FRAME_FORMAT_YUYV;
    return UVC_FRAME_FORMAT_ANY;
}

const char* formatToFourcc(uvc_frame_format fmt) {
    switch (fmt) {
        case UVC_FRAME_FORMAT_GRAY8: return "Y800";
        case UVC_FRAME_FORMAT_SGRBG8: return "GRBG";
        case UVC_FRAME_FORMAT_SRGGB8: return "RGGB";
        case UVC_FRAME_FORMAT_SGBRG8: return "GBRG";
        case UVC_FRAME_FORMAT_SBGGR8: return "BGGR";
        case UVC_FRAME_FORMAT_YUYV: return "YUY2";
        default: return "????";
    }
}
}  // namespace

UvcCapture& UvcCapture::instance() {
    static UvcCapture inst;
    return inst;
}

bool UvcCapture::init() {
    if (uvcCtx_) return true;
    uvc_error_t res = uvc_init(reinterpret_cast<uvc_context_t**>(&uvcCtx_), nullptr);
    if (res != UVC_SUCCESS) {
        LOGE("uvc_init failed: %s", uvc_strerror(res));
        return false;
    }
    LOGI("uvc_init OK");
    return true;
}

void UvcCapture::release() {
    close();
    if (uvcCtx_) {
        uvc_exit(reinterpret_cast<uvc_context_t*>(uvcCtx_));
        uvcCtx_ = nullptr;
    }
}

int UvcCapture::open(int fd, int busNum, int devAddr, int vid, int pid, const char* /*preferredFormat*/) {
    if (!uvcCtx_ && !init()) return -1;

    uvc_device_t* dev = nullptr;
    uvc_error_t res = uvc_get_device_with_fd(
        reinterpret_cast<uvc_context_t*>(uvcCtx_),
        &dev,
        vid, pid, nullptr,
        fd, busNum, devAddr);

    if (res != UVC_SUCCESS) {
        LOGE("uvc_get_device_with_fd failed: %s", uvc_strerror(res));
        return -2;
    }

    uvc_device_handle_t* handle = nullptr;
    res = uvc_open(dev, &handle);
    if (res != UVC_SUCCESS) {
        LOGE("uvc_open failed: %s", uvc_strerror(res));
        uvc_unref_device(dev);
        return -3;
    }

    uvcDev_ = dev;
    uvcHandle_ = handle;
    fd_ = fd;

    uvc_print_diag(handle, stderr);
    LOGI("UVC device opened vid=%04x pid=%04x fd=%d", vid, pid, fd);
    return 0;
}

void UvcCapture::close() {
    stopStream();
    if (uvcHandle_) {
        uvc_close(reinterpret_cast<uvc_device_handle_t*>(uvcHandle_));
        uvcHandle_ = nullptr;
    }
    if (uvcDev_) {
        uvc_unref_device(reinterpret_cast<uvc_device_t*>(uvcDev_));
        uvcDev_ = nullptr;
    }
    fd_ = -1;
}

int UvcCapture::startStream(int width, int height, const char* fourcc) {
    if (!uvcHandle_) return -1;
    stopStream();

    auto* handle = reinterpret_cast<uvc_device_handle_t*>(uvcHandle_);
    uvc_stream_ctrl_t ctrl{};
    uvc_frame_format fmt = fourccToFormat(fourcc);

    uvc_error_t res = uvc_get_stream_ctrl_format_size(
        handle, &ctrl, fmt, width, height, 0);

    if (res != UVC_SUCCESS && fmt != UVC_FRAME_FORMAT_ANY) {
        LOGI("Format %s %dx%d unavailable, trying ANY", fourcc, width, height);
        res = uvc_get_stream_ctrl_format_size(
            handle, &ctrl, UVC_FRAME_FORMAT_ANY, width, height, 0);
    }

    if (res != UVC_SUCCESS) {
        LOGE("uvc_get_stream_ctrl_format_size failed: %s", uvc_strerror(res));
        return -2;
    }

    uvc_stream_handle_t* stream = nullptr;
    res = uvc_start_streaming(handle, &ctrl, frameCallback, this, 0);
    if (res != UVC_SUCCESS) {
        LOGE("uvc_start_streaming failed: %s", uvc_strerror(res));
        return -3;
    }

    // Retrieve stream handle via internal reference after start
    // libuvc stores stream in handle; we track via callback state
    uvcStream_ = stream;
    streaming_.store(true);
    streamStartNs_ = nowNs();
    lastFpsTickNs_ = streamStartNs_;
    fpsFrameCount_ = 0;

    {
        std::lock_guard lock(mutex_);
        metrics_.frame_width = width;
        metrics_.frame_height = height;
        metrics_.fourcc = 0;
        const char* fc = fourccToFormat(fourcc) == UVC_FRAME_FORMAT_ANY ? "????" : fourcc;
        if (fc && strlen(fc) >= 4) {
            metrics_.fourcc = *reinterpret_cast<const uint32_t*>(fc);
        }
    }

    LOGI("Stream started: %s %dx%d", fourcc, width, height);
    return 0;
}

void UvcCapture::stopStream() {
    if (!streaming_.load()) return;
    streaming_.store(false);

    if (uvcHandle_) {
        uvc_stop_streaming(reinterpret_cast<uvc_device_handle_t*>(uvcHandle_));
    }
    uvcStream_ = nullptr;
    LOGI("Stream stopped");
}

void UvcCapture::frameCallback(uvc_frame_t* frame, void* userPtr) {
    auto* self = static_cast<UvcCapture*>(userPtr);
    self->onFrame(frame);
}

void UvcCapture::onFrame(uvc_frame_t* frame) {
    if (!frame || !frame->data || frame->data_bytes == 0) {
        std::lock_guard lock(mutex_);
        metrics_.dropped_frames++;
        return;
    }

    const uint64_t receiveNs = nowNs();
    const uint64_t captureStartNs = streamStartNs_;

    {
        std::lock_guard lock(mutex_);
        if (frameBuffer_.size() < frame->data_bytes) {
            frameBuffer_.resize(frame->data_bytes);
        }
        memcpy(frameBuffer_.data(), frame->data, frame->data_bytes);

        metrics_.total_frames++;
        metrics_.frame_width = static_cast<int>(frame->width);
        metrics_.frame_height = static_cast<int>(frame->height);
        metrics_.fourcc = *reinterpret_cast<const uint32_t*>(formatToFourcc(frame->frame_format));
        metrics_.latency_ms = static_cast<double>(receiveNs - captureStartNs) / 1e6;

        fpsFrameCount_++;
        const uint64_t elapsed = receiveNs - lastFpsTickNs_;
        if (elapsed >= 1'000'000'000ULL) {
            metrics_.fps = static_cast<double>(fpsFrameCount_) * 1e9 / static_cast<double>(elapsed);
            metrics_.usb_mbps = (metrics_.fps * frame->data_bytes * 8.0) / 1e6;
            fpsFrameCount_ = 0;
            lastFpsTickNs_ = receiveNs;
        }
    }
}

size_t UvcCapture::copyLatestFrame(uint8_t* dst, size_t capacity, CaptureMetrics* outMeta) {
    std::lock_guard lock(mutex_);
    if (frameBuffer_.empty() || frameBuffer_.size() > capacity) return 0;
    memcpy(dst, frameBuffer_.data(), frameBuffer_.size());
    if (outMeta) *outMeta = metrics_;
    return frameBuffer_.size();
}

CaptureMetrics UvcCapture::getMetrics() const {
    std::lock_guard lock(mutex_);
    return metrics_;
}

std::string UvcCapture::enumerateFormats() {
    if (!uvcHandle_) return "Device not open";

    auto* handle = reinterpret_cast<uvc_device_handle_t*>(uvcHandle_);
    std::ostringstream oss;

    const uvc_format_desc_t* fmt = uvc_get_format_descs(handle);
    while (fmt) {
        char fourcc[5] = {0};
        memcpy(fourcc, fmt->fourccFormat, 4);
        const uvc_frame_desc_t* frame = fmt->frame_descs;
        while (frame) {
            oss << fourcc << "|" << frame->wWidth << "x" << frame->wHeight;
            if (frame->dwDefaultFrameInterval > 0) {
                const double fps = 10000000.0 / frame->dwDefaultFrameInterval;
                oss << "|" << static_cast<int>(fps) << "fps";
            }
            oss << ";";
            frame = frame->next;
        }
        fmt = fmt->next;
    }
    return oss.str();
}
