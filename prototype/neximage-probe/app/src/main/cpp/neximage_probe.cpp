#include <jni.h>
#include <android/log.h>

#include <string>

#include "gl_preview.h"
#include "uvc_capture.h"

#define LOG_TAG "NexImageProbe"

extern std::string analyzePixelFormat(const std::string& identifier);

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeInit(JNIEnv*, jobject) {
    return UvcCapture::instance().init() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeRelease(JNIEnv*, jobject) {
    UvcCapture::instance().release();
}

JNIEXPORT jint JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeOpen(
    JNIEnv* env, jobject,
    jint fd, jint busNum, jint devAddr, jint vid, jint pid, jstring preferredFormat) {
    const char* fmt = env->GetStringUTFChars(preferredFormat, nullptr);
    const int result = UvcCapture::instance().open(fd, busNum, devAddr, vid, pid, fmt);
    env->ReleaseStringUTFChars(preferredFormat, fmt);
    return result;
}

JNIEXPORT void JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeClose(JNIEnv*, jobject) {
    UvcCapture::instance().close();
}

JNIEXPORT jint JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeStartStream(
    JNIEnv* env, jobject, jint width, jint height, jstring fourcc) {
    const char* fc = env->GetStringUTFChars(fourcc, nullptr);
    const int result = UvcCapture::instance().startStream(width, height, fc);
    env->ReleaseStringUTFChars(fourcc, fc);
    return result;
}

JNIEXPORT void JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeStopStream(JNIEnv*, jobject) {
    UvcCapture::instance().stopStream();
}

JNIEXPORT void JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeAttachPreview(JNIEnv*, jobject) {
    GlPreview::instance().attach();
}

JNIEXPORT void JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeDetachPreview(JNIEnv*, jobject) {
    GlPreview::instance().detach();
}

JNIEXPORT jboolean JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeRenderFrame(JNIEnv*, jobject) {
    return GlPreview::instance().renderFrame() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jdoubleArray JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeGetMetrics(JNIEnv* env, jobject) {
    const CaptureMetrics m = UvcCapture::instance().getMetrics();
    const jdouble arr[11] = {
        m.fps,
        m.usb_mbps,
        m.latency_ms,
        static_cast<jdouble>(m.dropped_frames),
        static_cast<jdouble>(m.total_frames),
        0.0,  // CPU filled on Kotlin side if needed
        static_cast<jdouble>(m.native_heap_kb),
        static_cast<jdouble>(m.frame_width),
        static_cast<jdouble>(m.frame_height),
        static_cast<jdouble>(m.fourcc),
        0.0,
    };

    jdoubleArray result = env->NewDoubleArray(11);
    env->SetDoubleArrayRegion(result, 0, 11, arr);
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeEnumerateFormats(JNIEnv* env, jobject) {
    const std::string formats = UvcCapture::instance().enumerateFormats();
    return env->NewStringUTF(formats.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_astro_neximage_probe_NativeBridge_nativeAnalyzePixelFormat(JNIEnv* env, jobject, jstring identifier) {
    const char* id = env->GetStringUTFChars(identifier, nullptr);
    const std::string analysis = analyzePixelFormat(id);
    env->ReleaseStringUTFChars(identifier, id);
    return env->NewStringUTF(analysis.c_str());
}

}  // extern "C"
