package com.astro.neximage.probe

object NativeBridge {
    init {
        System.loadLibrary("neximage_probe")
    }

    external fun nativeInit(): Boolean
    external fun nativeRelease()

    /** Open UVC device using Android USB fd. Returns 0 on success. */
    external fun nativeOpen(
        fd: Int,
        busNum: Int,
        devAddr: Int,
        vid: Int,
        pid: Int,
        preferredFormat: String,
    ): Int

    external fun nativeClose()
    external fun nativeStartStream(width: Int, height: Int, fourcc: String): Int
    external fun nativeStopStream()

    /** Attach OpenGL preview to native renderer. Call from GL thread. */
    external fun nativeAttachPreview()
    external fun nativeDetachPreview()
    external fun nativeRenderFrame(): Boolean

    /** Poll latest metrics from native layer. */
    external fun nativeGetMetrics(): DoubleArray

    /** Enumerate UVC formats. Returns flat string: "FOURCC|WxH|fps;..." */
    external fun nativeEnumerateFormats(): String

    /** Returns pixel format analysis for a GUID or FOURCC string. */
    external fun nativeAnalyzePixelFormat(identifier: String): String
}
