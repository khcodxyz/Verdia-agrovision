#pragma once
// ============================================================
// inference.h - Envoltorio de TensorFlow Lite Micro
// El resto del programa no incluye cabeceras de TFLM: solo esta API.
// ============================================================
#include <stdint.h>
#include "config.h"

struct InferenceResult {
    float    scores[NUM_CLASSES];  // probabilidades desquantizadas (0..1)
    int      topIndex;             // clase con mayor score
    float    topScore;
    uint32_t inferenceTimeMs;
};

// Etiquetas en el MISMO orden que la capa de salida del modelo.
extern const char* const kClassLabels[NUM_CLASSES];

// Carga el modelo, crea el intérprete y reserva la arena en PSRAM.
bool inference_init();

// Acceso al buffer de entrada (int8, W*H*CH) y a sus parámetros de
// cuantización, para que image_utils rellene el tensor directamente.
int8_t* inference_input_buffer();
float   inference_input_scale();
int     inference_input_zero_point();

// Ejecuta Invoke() y desquantiza la salida.
bool inference_run(InferenceResult& out);
