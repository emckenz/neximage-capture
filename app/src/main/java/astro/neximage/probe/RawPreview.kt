package astro.neximage.probe

import android.content.Context
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class RawPreview(context: Context) : GLSurfaceView(context) {
    private val renderer = BayerRenderer()

    init {
        setEGLContextClientVersion(3)
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    var phase: Int
        get() = renderer.phase
        set(value) {
            renderer.phase = value
        }

    fun submit(src: ByteBuffer, width: Int, height: Int, length: Int) {
        renderer.submit(src, width, height, length)
    }

    fun ageMs(): Long = renderer.ageMs()
}

private class BayerRenderer : GLSurfaceView.Renderer {
    @Volatile var phase: Int = 0

    private val lock = Any()
    private var pending: ByteArray? = null
    private var pendingW = 0
    private var pendingH = 0
    private var pendingLen = 0
    private var dirty = false
    private var arrivedNs = 0L
    private var drawnNs = 0L

    private var program = 0
    private var texture = 0
    private var phaseLoc = 0
    private var sizeLoc = 0
    private var texW = 0
    private var texH = 0
    private var quad: FloatBuffer? = null

    fun submit(src: ByteBuffer, width: Int, height: Int, length: Int) {
        if (length <= 0 || width <= 0 || height <= 0) return
        val copy = ByteArray(length)
        val dup = src.duplicate()
        dup.clear()
        dup.get(copy, 0, length)
        synchronized(lock) {
            pending = copy
            pendingW = width
            pendingH = height
            pendingLen = length
            dirty = true
            arrivedNs = System.nanoTime()
        }
    }

    fun ageMs(): Long {
        val drawn = drawnNs
        val arrived = arrivedNs
        if (drawn == 0L || arrived == 0L || drawn < arrived) return -1L
        return (drawn - arrived) / 1_000_000L
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        GLES30.glClearColor(0f, 0f, 0f, 1f)
        program = link(VERT, FRAG)
        phaseLoc = GLES30.glGetUniformLocation(program, "phase")
        sizeLoc = GLES30.glGetUniformLocation(program, "size")
        val ids = IntArray(1)
        GLES30.glGenTextures(1, ids, 0)
        texture = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        val verts = floatArrayOf(
            -1f, -1f, 0f, 1f,
            1f, -1f, 1f, 1f,
            -1f, 1f, 0f, 0f,
            1f, 1f, 1f, 0f,
        )
        quad = ByteBuffer.allocateDirect(verts.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
        quad?.put(verts)?.position(0)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        GLES30.glViewport(0, 0, width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        var pixels: ByteArray? = null
        var w = 0
        var h = 0
        var len = 0
        synchronized(lock) {
            if (dirty && pending != null) {
                pixels = pending
                w = pendingW
                h = pendingH
                len = pendingLen
                dirty = false
            }
        }
        if (pixels != null && w > 0 && h > 0 && len >= w * h) {
            val buf = ByteBuffer.allocateDirect(w * h).order(ByteOrder.nativeOrder())
            buf.put(pixels, 0, w * h)
            buf.position(0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture)
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1)
            if (w != texW || h != texH) {
                GLES30.glTexImage2D(
                    GLES30.GL_TEXTURE_2D, 0, GLES30.GL_R8, w, h, 0,
                    GLES30.GL_RED, GLES30.GL_UNSIGNED_BYTE, buf,
                )
                texW = w
                texH = h
            } else {
                GLES30.glTexSubImage2D(
                    GLES30.GL_TEXTURE_2D, 0, 0, 0, w, h,
                    GLES30.GL_RED, GLES30.GL_UNSIGNED_BYTE, buf,
                )
            }
            drawnNs = System.nanoTime()
        }
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(program)
        GLES30.glUniform1i(phaseLoc, phase)
        GLES30.glUniform2i(sizeLoc, texW.coerceAtLeast(1), texH.coerceAtLeast(1))
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture)
        val q = quad ?: return
        q.position(0)
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 16, q)
        q.position(2)
        GLES30.glEnableVertexAttribArray(1)
        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 16, q)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)
    }

    private fun link(vert: String, frag: String): Int {
        fun compile(type: Int, src: String): Int {
            val id = GLES30.glCreateShader(type)
            GLES30.glShaderSource(id, src)
            GLES30.glCompileShader(id)
            return id
        }
        val programId = GLES30.glCreateProgram()
        GLES30.glAttachShader(programId, compile(GLES30.GL_VERTEX_SHADER, vert))
        GLES30.glAttachShader(programId, compile(GLES30.GL_FRAGMENT_SHADER, frag))
        GLES30.glBindAttribLocation(programId, 0, "pos")
        GLES30.glBindAttribLocation(programId, 1, "uv")
        GLES30.glLinkProgram(programId)
        return programId
    }

    companion object {
        private const val VERT = """
            #version 300 es
            layout(location = 0) in vec2 pos;
            layout(location = 1) in vec2 uv;
            out vec2 vUv;
            void main() {
                vUv = uv;
                gl_Position = vec4(pos, 0.0, 1.0);
            }
        """
        private const val FRAG = """
            #version 300 es
            precision mediump float;
            uniform sampler2D rawTex;
            uniform ivec2 size;
            uniform int phase;
            in vec2 vUv;
            out vec4 frag;
            void main() {
                ivec2 p = ivec2(vUv * vec2(size));
                int x = p.x & ~1;
                int y = p.y & ~1;
                if (x + 1 >= size.x) x = max(size.x - 2, 0);
                if (y + 1 >= size.y) y = max(size.y - 2, 0);
                float p00 = texelFetch(rawTex, ivec2(x, y), 0).r;
                float p10 = texelFetch(rawTex, ivec2(x + 1, y), 0).r;
                float p01 = texelFetch(rawTex, ivec2(x, y + 1), 0).r;
                float p11 = texelFetch(rawTex, ivec2(x + 1, y + 1), 0).r;
                float r; float g; float b;
                if (phase == 0) { g = (p00 + p11) * 0.5; r = p10; b = p01; }
                else if (phase == 1) { g = (p00 + p11) * 0.5; b = p10; r = p01; }
                else if (phase == 2) { r = p00; b = p11; g = (p10 + p01) * 0.5; }
                else { b = p00; r = p11; g = (p10 + p01) * 0.5; }
                frag = vec4(r, g, b, 1.0);
            }
        """
    }
}
