// ============================================================
// inference.cpp - TensorFlow Lite Micro en ESP32
// ============================================================
#include "inference.h"
#include "VerdIA_model.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

// Libreria "Chirale_TensorFlowLite" (Gestor de librerias de Arduino IDE)
#include <Chirale_TensorFlowLite.h>

// Algunas distribuciones Arduino de TFLM (p. ej. TensorFlowLite_ESP32)
// exigen incluir su cabecera principal antes que el resto.
#if __has_include(<TensorFlowLite_ESP32.h>)
  #include <TensorFlowLite_ESP32.h>
#elif __has_include(<TensorFlowLite.h>)
  #include <TensorFlowLite.h>
#endif

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#if __has_include("tensorflow/lite/version.h")
  #include "tensorflow/lite/version.h"
#endif

// MicroErrorReporter fue eliminado de tflite-micro en 2023. Detectamos
// la versión por la presencia de su cabecera. Si tu librería es distinta
// y no compila aquí, ajusta el constructor del intérprete manualmente.
#if __has_include("tensorflow/lite/micro/micro_error_reporter.h")
  #include "tensorflow/lite/micro/micro_error_reporter.h"
  #define TFLM_LEGACY_ERROR_REPORTER 1
#else
  #define TFLM_LEGACY_ERROR_REPORTER 0
#endif

// ---------- Etiquetas (orden = salida del modelo) ----------
const char* const kClassLabels[NUM_CLASSES] = {
    "Saludable",
    "Mancha bacteriana",
    "Tizon temprano",
    "Tizon tardio",
    "Moho de hoja",
    "Mancha Septoria"
};

namespace {
    // Operadores que necesita el modelo. Un modelo MobileNet int8 típico
    // usa CONV_2D, DEPTHWISE_CONV_2D, AVERAGE_POOL_2D/MEAN, RESHAPE,
    // FULLY_CONNECTED, SOFTMAX y a veces PAD/ADD. Comprueba la lista real
    // en Netron: cada operador ausente hace fallar AllocateTensors().
    // No usamos AllOpsResolver: ya no existe en versiones recientes y
    // además gastaría cientos de KB de Flash.
    constexpr int kNumOps = 14;
    using Resolver = tflite::MicroMutableOpResolver<kNumOps>;

    const tflite::Model*       g_tfModel     = nullptr;
    tflite::MicroInterpreter*  g_interpreter = nullptr;
    TfLiteTensor*              g_input       = nullptr;
    TfLiteTensor*              g_output      = nullptr;
    uint8_t*                   g_arena       = nullptr;

    // Reservados con duración estática para que sobrevivan a init().
    alignas(16) uint8_t g_resolverStorage[sizeof(Resolver)];
    alignas(16) uint8_t g_interpStorage[sizeof(tflite::MicroInterpreter)];
#if TFLM_LEGACY_ERROR_REPORTER
    tflite::MicroErrorReporter g_errorReporter;
#endif

    bool registerOps(Resolver& r) {
        // Cada Add* devuelve TfLiteStatus; comprobamos el primero que falle.
        if (r.AddConv2D()          != kTfLiteOk) return false;
        if (r.AddDepthwiseConv2D() != kTfLiteOk) return false;
        if (r.AddFullyConnected()  != kTfLiteOk) return false;
        if (r.AddSoftmax()         != kTfLiteOk) return false;
        if (r.AddReshape()         != kTfLiteOk) return false;
        if (r.AddAveragePool2D()   != kTfLiteOk) return false;
        if (r.AddMaxPool2D()       != kTfLiteOk) return false;
        if (r.AddMean()            != kTfLiteOk) return false;
        if (r.AddPad()             != kTfLiteOk) return false;
        if (r.AddAdd()             != kTfLiteOk) return false;
        if (r.AddMul()             != kTfLiteOk) return false;
        if (r.AddQuantize()        != kTfLiteOk) return false;
        if (r.AddDequantize()      != kTfLiteOk) return false;
        if (r.AddLogistic()        != kTfLiteOk) return false;
        return true;
    }
}

bool inference_init() {
    // 0. Rechazar el placeholder antes de entregárselo al parser FlatBuffer.
    // Un .tflite válido lleva el identificador "TFL3" en bytes 4..7.
    if (g_model_len < 1024 || g_model[4] != 'T' || g_model[5] != 'F' ||
        g_model[6] != 'L' || g_model[7] != '3') {
        Serial.println("[TFLM] VerdIA_model.h no contiene un modelo TFLite válido. Ejecuta el entrenamiento primero.");
        return false;
    }

    // 1. Mapear el modelo desde Flash (no se copia a RAM)
    g_tfModel = tflite::GetModel(g_model);
    if (g_tfModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("[TFLM] Version de esquema %lu != %d. ¿VerdIA_model.h es un .tflite real?\n",
                      (unsigned long)g_tfModel->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // 2. Resolver de operadores
    Resolver* resolver = new (g_resolverStorage) Resolver();
    if (!registerOps(*resolver)) {
        Serial.println("[TFLM] Error registrando operadores");
        return false;
    }

    // 3. Tensor arena en PSRAM. Es la ÚNICA memoria dinámica que usa TFLM:
    //    activaciones, buffers temporales y metadatos viven aquí, con
    //    tamaño fijo -> sin fragmentación ni fugas durante la operación.
    //    16 bytes de alineación es lo que exige TFLM.
    g_arena = (uint8_t*)heap_caps_aligned_alloc(16, TENSOR_ARENA_SIZE,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_arena) {
        Serial.println("[TFLM] Sin PSRAM para la arena. ¿PSRAM habilitada?");
        return false;
    }

    // 4. Intérprete
#if TFLM_LEGACY_ERROR_REPORTER
    g_interpreter = new (g_interpStorage) tflite::MicroInterpreter(
        g_tfModel, *resolver, g_arena, TENSOR_ARENA_SIZE, &g_errorReporter);
#else
    g_interpreter = new (g_interpStorage) tflite::MicroInterpreter(
        g_tfModel, *resolver, g_arena, TENSOR_ARENA_SIZE);
#endif

    if (g_interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("[TFLM] AllocateTensors() fallo: arena pequeña u operador faltante");
        return false;
    }

    g_input  = g_interpreter->input(0);
    g_output = g_interpreter->output(0);

    // 5. Validar que el modelo coincide con config.h
    if (g_input->type != kTfLiteInt8 || g_output->type != kTfLiteInt8) {
        Serial.println("[TFLM] El modelo debe estar cuantizado a int8 (entrada y salida)");
        return false;
    }
    if (g_input->dims->size != 4 ||
        g_input->dims->data[1] != MODEL_INPUT_H ||
        g_input->dims->data[2] != MODEL_INPUT_W ||
        g_input->dims->data[3] != MODEL_INPUT_CH) {
        Serial.printf("[TFLM] Entrada del modelo [1,%d,%d,%d] != config [1,%d,%d,%d]\n",
                      g_input->dims->data[1], g_input->dims->data[2], g_input->dims->data[3],
                      MODEL_INPUT_H, MODEL_INPUT_W, MODEL_INPUT_CH);
        return false;
    }
    if (g_output->dims->data[g_output->dims->size - 1] != NUM_CLASSES) {
        Serial.println("[TFLM] Numero de clases del modelo != NUM_CLASSES");
        return false;
    }

    Serial.printf("[TFLM] OK. Arena usada: %u / %u bytes | in scale=%.6f zp=%d\n",
                  (unsigned)g_interpreter->arena_used_bytes(), (unsigned)TENSOR_ARENA_SIZE,
                  g_input->params.scale, (int)g_input->params.zero_point);
    return true;
}

int8_t* inference_input_buffer()     { return g_input ? g_input->data.int8 : nullptr; }
float   inference_input_scale()      { return g_input ? g_input->params.scale : 1.0f; }
int     inference_input_zero_point() { return g_input ? g_input->params.zero_point : 0; }

bool inference_run(InferenceResult& out) {
    if (!g_interpreter) return false;

    const uint32_t t0 = millis();
    if (g_interpreter->Invoke() != kTfLiteOk) {
        Serial.println("[TFLM] Invoke() fallo");
        return false;
    }
    out.inferenceTimeMs = millis() - t0;

    // Desquantizar la salida: x = (q - zero_point) * scale
    const float oScale = g_output->params.scale;
    const int   oZp    = g_output->params.zero_point;
    out.topIndex = 0;
    out.topScore = -1.0f;
    for (int i = 0; i < NUM_CLASSES; ++i) {
        const float s = (g_output->data.int8[i] - oZp) * oScale;
        out.scores[i] = s;
        if (s > out.topScore) { out.topScore = s; out.topIndex = i; }
    }
    return true;
}
