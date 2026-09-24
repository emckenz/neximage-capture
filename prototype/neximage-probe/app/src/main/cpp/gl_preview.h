#pragma once

#include <cstdint>

class GlPreview {
public:
    static GlPreview& instance();

    void attach();
    void detach();
    bool renderFrame();

private:
    GlPreview() = default;
    void initGl();
    void uploadAndDraw(const uint8_t* data, int width, int height, uint32_t fourcc);

    bool initialized_ = false;
    unsigned int program_ = 0;
    unsigned int texture_ = 0;
    unsigned int vbo_ = 0;
};
