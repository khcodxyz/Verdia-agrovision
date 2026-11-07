// ============================================================
// image_utils.cpp
// ============================================================
#include "image_utils.h"
#include "config.h"
#include <math.h>

// Orden de bytes RGB565 tal como lo entrega esp32-camera (byte alto primero).
// Tomado de la conversión RGB565->RGB888 de to_bmp.c en esp32-camera.
// Si los colores del dashboard salen "invertidos" (azul<->rojo), intercambia
// p[0] y p[1] aquí.
static inline void rgb565_to_rgb888(const uint8_t* p, uint8_t& r, uint8_t& g, uint8_t& b) {
    const uint16_t v = ((uint16_t)p[0] << 8) | p[1];
    const uint8_t r5 = (v >> 11) & 0x1F;
    const uint8_t g6 = (v >> 5)  & 0x3F;
    const uint8_t b5 = v & 0x1F;
    // Expansión de bits, no solo desplazamiento: 31->255 y 63->255.
    r = (uint8_t)((r5 << 3) | (r5 >> 2));
    g = (uint8_t)((g6 << 2) | (g6 >> 4));
    b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

// Construye la tabla píxel(0..255) -> valor cuantizado int8.
static void build_quant_lut(int8_t lut[256], float scale, int zeroPoint) {
    for (int v = 0; v < 256; ++v) {
        float x;
#if   MODEL_INPUT_NORMALIZATION == 0
        x = (float)v;                    // [0,255]
#elif MODEL_INPUT_NORMALIZATION == 1
        x = (float)v / 255.0f;           // [0,1]
#else
        x = (float)v / 127.5f - 1.0f;    // [-1,1]
#endif
        int q = (int)lroundf(x / scale) + zeroPoint;
        if (q > 127)  q = 127;
        if (q < -128) q = -128;
        lut[v] = (int8_t)q;
    }
}

bool preprocess_rgb565_to_int8(const uint8_t* src, int srcW, int srcH,
                               int8_t* dst, int dstW, int dstH, int dstCh,
                               float inScale, int inZeroPoint) {
    if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return false;
    if (dstCh != 1 && dstCh != 3) return false;
    if (inScale <= 0.0f) return false;

    int8_t lut[256];
    build_quant_lut(lut, inScale, inZeroPoint);

    // Recorte central cuadrado
    const int side  = (srcW < srcH) ? srcW : srcH;
    const int cropX = (srcW - side) / 2;
    const int cropY = (srcH - side) / 2;

    for (int dy = 0; dy < dstH; ++dy) {
        // Rango de filas de origen que caen en esta fila destino
        int sy0 = cropY + (dy * side) / dstH;
        int sy1 = cropY + ((dy + 1) * side) / dstH;
        if (sy1 <= sy0) sy1 = sy0 + 1;

        for (int dx = 0; dx < dstW; ++dx) {
            int sx0 = cropX + (dx * side) / dstW;
            int sx1 = cropX + ((dx + 1) * side) / dstW;
            if (sx1 <= sx0) sx1 = sx0 + 1;

            uint32_t accR = 0, accG = 0, accB = 0;
            uint32_t n = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const uint8_t* row = src + ((size_t)sy * srcW + sx0) * 2;
                for (int sx = sx0; sx < sx1; ++sx, row += 2) {
                    uint8_t r, g, b;
                    rgb565_to_rgb888(row, r, g, b);
                    accR += r; accG += g; accB += b; ++n;
                }
            }
            const uint8_t r = (uint8_t)(accR / n);
            const uint8_t g = (uint8_t)(accG / n);
            const uint8_t b = (uint8_t)(accB / n);

            int8_t* out = dst + ((size_t)dy * dstW + dx) * dstCh;
            if (dstCh == 3) {
                out[0] = lut[r];
                out[1] = lut[g];
                out[2] = lut[b];
            } else {
                // Luminancia Rec.601 en enteros: Y = 0.299R + 0.587G + 0.114B
                const uint8_t y = (uint8_t)((77u * r + 150u * g + 29u * b) >> 8);
                out[0] = lut[y];
            }
        }
    }
    return true;
}
