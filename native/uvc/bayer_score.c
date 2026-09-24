#include "bayer_score.h"

#include <stdlib.h>

static int sat_abs(int v) {
    return v < 0 ? -v : v;
}

BayerScore bayer_score_8(const unsigned char *pixels, int width, int height) {
    BayerScore score;
    long neighbor = 0;
    long same = 0;
    int samples = 0;
    int y;
    int x0;
    int y0;
    int x1;
    int y1;

    score.neighbor = 0;
    score.same_color = 0;
    score.ratio = 0;
    score.likely_mosaic = 0;
    score.low_byte_nonzero = 0;
    score.samples = 0;
    if (width < 8 || height < 8 || pixels == NULL) return score;
    x0 = width / 4;
    y0 = height / 4;
    x1 = width - width / 4 - 2;
    y1 = height - height / 4 - 2;
    if (x1 <= x0 || y1 <= y0) {
        x0 = 0;
        y0 = 0;
        x1 = width - 2;
        y1 = height - 2;
    }
    for (y = y0; y < y1; y += 2) {
        int x;
        for (x = x0; x < x1; x += 2) {
            int p = pixels[y * width + x];
            neighbor += sat_abs(p - pixels[y * width + x + 1]);
            neighbor += sat_abs(p - pixels[(y + 1) * width + x]);
            same += sat_abs(p - pixels[y * width + x + 2]);
            same += sat_abs(p - pixels[(y + 2) * width + x]);
            samples++;
        }
    }
    score.samples = samples;
    if (samples == 0) return score;
    score.neighbor = (double)neighbor / (double)(samples * 2);
    score.same_color = (double)same / (double)(samples * 2);
    if (score.same_color < 0.001) {
        score.ratio = score.neighbor > 1.0 ? 99.0 : 0.0;
    } else {
        score.ratio = score.neighbor / score.same_color;
    }
    score.likely_mosaic = score.neighbor > 1.0 && score.ratio > 1.25;
    return score;
}

BayerScore bayer_score_16_low_byte(const unsigned char *pixels, int width, int height) {
    BayerScore score = bayer_score_8(NULL, 0, 0);
    int n = width * height;
    int i;
    int nonzero = 0;
    unsigned char *hi;
    if (pixels == NULL || n <= 0) return score;
    for (i = 0; i < n; i++) {
        if (pixels[i * 2] != 0) nonzero++;
    }
    hi = (unsigned char *)malloc((size_t)n);
    if (!hi) {
        score.low_byte_nonzero = nonzero;
        return score;
    }
    for (i = 0; i < n; i++) hi[i] = pixels[i * 2 + 1];
    score = bayer_score_8(hi, width, height);
    score.low_byte_nonzero = nonzero;
    free(hi);
    return score;
}
