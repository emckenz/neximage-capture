#include "gl_preview.h"

#include "uvc_capture.h"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <vector>

#define LOG_TAG "GlPreview"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTex;
out vec2 vTex;
void main() {
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Debayer GRBG preview (bilinear approximation for probe — full Malvar-He in production)
const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec2 vTex;
out vec4 fragColor;
uniform sampler2D uBayer;
uniform ivec2 uSize;
uniform int uIsBayer;

vec3 debayerGRBG(vec2 uv) {
    vec2 pix = uv * vec2(uSize);
    ivec2 p = ivec2(floor(pix));
    vec2 f = fract(pix);
    float tl = texelFetch(uBayer, p, 0).r;
    float tr = texelFetch(uBayer, p + ivec2(1, 0), 0).r;
    float bl = texelFetch(uBayer, p + ivec2(0, 1), 0).r;
    float br = texelFetch(uBayer, p + ivec2(1, 1), 0).r;
    bool evenRow = (p.y & 1) == 0;
    bool evenCol = (p.x & 1) == 0;
    float r, g, b;
    if (evenRow && !evenCol) { r = tr; g = mix(tl, bl, f.y); b = bl; }
    else if (!evenRow && evenCol) { r = tl; g = mix(tr, br, f.y); b = br; }
    else if (evenRow && evenCol) { g = tl; r = mix(tr, tl, f.x); b = mix(bl, tl, f.y); }
    else { g = br; r = mix(bl, br, f.x); b = br; }
    return vec3(r, g, b);
}

void main() {
    if (uIsBayer == 1) {
        fragColor = vec4(debayerGRBG(vTex), 1.0);
    } else {
        float g = texture(uBayer, vTex).r;
        fragColor = vec4(g, g, g, 1.0);
    }
}
)";

unsigned int compileShader(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    return shader;
}

unsigned int linkProgram(unsigned int vs, unsigned int fs) {
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    return prog;
}
}  // namespace

GlPreview& GlPreview::instance() {
    static GlPreview inst;
    return inst;
}

void GlPreview::attach() {
    if (!initialized_) initGl();
}

void GlPreview::detach() {
    if (texture_) glDeleteTextures(1, &texture_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
    texture_ = vbo_ = program_ = 0;
    initialized_ = false;
}

void GlPreview::initGl() {
    const unsigned int vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    program_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const float quad[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f,
    };
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    initialized_ = true;
}

bool GlPreview::renderFrame() {
    if (!initialized_) return false;

    static std::vector<uint8_t> buffer(16 * 1024 * 1024);
    CaptureMetrics meta{};
    const size_t bytes = UvcCapture::instance().copyLatestFrame(buffer.data(), buffer.size(), &meta);
    if (bytes == 0 || meta.frame_width <= 0 || meta.frame_height <= 0) return false;

    uploadAndDraw(buffer.data(), meta.frame_width, meta.frame_height, meta.fourcc);
    return true;
}

void GlPreview::uploadAndDraw(const uint8_t* data, int width, int height, uint32_t fourcc) {
    glViewport(0, 0, width, height);

    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data);

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uBayer"), 0);
    glUniform2i(glGetUniformLocation(program_, "uSize"), width, height);

    char fourccStr[5] = {
        static_cast<char>(fourcc & 0xFF),
        static_cast<char>((fourcc >> 8) & 0xFF),
        static_cast<char>((fourcc >> 16) & 0xFF),
        static_cast<char>((fourcc >> 24) & 0xFF),
        0,
    };
    const bool isBayer = (strncmp(fourccStr, "GRBG", 4) == 0 ||
                          strncmp(fourccStr, "RGGB", 4) == 0 ||
                          strncmp(fourccStr, "GBRG", 4) == 0 ||
                          strncmp(fourccStr, "BGGR", 4) == 0);
    glUniform1i(glGetUniformLocation(program_, "uIsBayer"), isBayer ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
