package com.astro.neximage.probe

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class PreviewSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : GLSurfaceView(context, attrs) {

    init {
        setEGLContextClientVersion(3)
        setRenderer(PreviewRenderer())
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    fun requestRenderFrame() {
        requestRender()
    }

    private class PreviewRenderer : Renderer {
        private var attached = false

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            if (!attached) {
                NativeBridge.nativeAttachPreview()
                attached = true
            }
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            // Viewport handled in native
        }

        override fun onDrawFrame(gl: GL10?) {
            if (NativeBridge.nativeRenderFrame()) {
                // frame rendered
            }
        }
    }

    override fun onDetachedFromWindow() {
        NativeBridge.nativeDetachPreview()
        super.onDetachedFromWindow()
    }
}
