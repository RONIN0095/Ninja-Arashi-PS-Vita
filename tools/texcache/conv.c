#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"
void eac_decode(uint8_t *, uint8_t *, const int, const int, int);
int main(int c, char **v) {
    int w = atoi(v[2]), h = atoi(v[3]);
    FILE *f = fopen(v[1], "rb"); uint8_t *in = malloc(w * h); fread(in, 1, w * h, f); fclose(f);
    uint8_t *rgba = malloc(w * h * 4); eac_decode(in, rgba, w, h, 0);
    uint8_t *dxt = malloc(w * h), *out = dxt, block[64];
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            for (int r = 0; r < 4; r++) memcpy(&block[r * 16], &rgba[((by + r) * w + bx) * 4], 16);
            stb_compress_dxt_block(out, block, 1, 0); out += 16;
        }
    f = fopen(v[4], "wb"); fwrite(dxt, 1, w * h, f); fclose(f); return 0;
}
