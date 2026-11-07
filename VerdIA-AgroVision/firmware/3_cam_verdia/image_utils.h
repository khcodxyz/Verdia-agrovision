#pragma once
// ============================================================
// image_utils.h - Conversión del frame de cámara al tensor del modelo
// ============================================================
#include <stdint.h>
#include <stddef.h>

// Convierte un frame RGB565 (srcW x srcH) al tensor de entrada int8:
//   1. Recorte central cuadrado (evita deformar la hoja).
//   2. Reducción por promedio de área (box filter) a dstW x dstH.
//   3. Conversión RGB -> RGB o -> gris según dstCh.
//   4. Normalización (MODEL_INPUT_NORMALIZATION) y cuantización:
//        q = round(x / inScale) + inZeroPoint, saturado a [-128, 127]
//
// Todo en aritmética entera salvo una LUT de 256 entradas que se calcula
// una vez por llamada: coste ~ O(srcW*srcH) y sin floats por píxel.
bool preprocess_rgb565_to_int8(const uint8_t* src, int srcW, int srcH,
                               int8_t* dst, int dstW, int dstH, int dstCh,
                               float inScale, int inZeroPoint);
