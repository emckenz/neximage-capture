#ifndef NEXIMAGE_BAYER_SCORE_H
#define NEXIMAGE_BAYER_SCORE_H

typedef struct BayerScore {
    double neighbor;
    double same_color;
    double ratio;
    int likely_mosaic;
    int low_byte_nonzero;
    int samples;
} BayerScore;

BayerScore bayer_score_8(const unsigned char *pixels, int width, int height);
BayerScore bayer_score_16_low_byte(const unsigned char *pixels, int width, int height);

#endif
