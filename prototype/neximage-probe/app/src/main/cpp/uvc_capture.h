#pragma once

#include "libuvc/libuvc.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

struct CaptureMetrics {
    double fps = 0;
    double usb_mbps = 0;
    double latency_ms = 0;
    uint64_t dropped_frames = 0;
    uint64_t total_frames = 0;
    double cpu_percent = 0;
    uint64_t native_heap_kb = 0;
    int frame_width = 0;
    int frame_height = 0;
    uint32_t fourcc = 0;
};

class UvcCapture {
public:
    static UvcCapture& instance();

    bool init();
    void release();

    int open(int fd, int busNum, int devAddr, int vid, int pid, const char* preferredFormat);
    void close();

    int startStream(int width, int height, const char* fourcc);
    void stopStream();

    std::string enumerateFormats();
    CaptureMetrics getMetrics() const;

    /** Copy latest frame into dst; returns bytes copied or 0 if none. Thread-safe. */
    size_t copyLatestFrame(uint8_t* dst, size_t capacity, CaptureMetrics* outMeta);

    bool isStreaming() const { return streaming_.load(); }

private:
    UvcCapture() = default;

    static void frameCallback(uvc_frame_t* frame, void* userPtr);

    void onFrame(uvc_frame_t* frame);

    mutable std::mutex mutex_;
    std::vector<uint8_t> frameBuffer_;
    CaptureMetrics metrics_;

    std::atomic<bool> streaming_{false};
    int fd_ = -1;
    void* uvcCtx_ = nullptr;
    void* uvcDev_ = nullptr;
    void* uvcHandle_ = nullptr;
    void* uvcStream_ = nullptr;

    uint64_t lastFpsTickNs_ = 0;
    uint64_t fpsFrameCount_ = 0;
    uint64_t streamStartNs_ = 0;
};
