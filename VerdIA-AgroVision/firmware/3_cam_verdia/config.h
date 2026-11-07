#pragma once
// ============================================================
// config.h - VerdIA Tomato-6 en ESP32-CAM (version para Arduino IDE)
// Valores tomados de tu entrenamiento (metrics.json / model_info.txt)
// ============================================================

// ---------- Modelo ----------
#define MODEL_INPUT_W    96
#define MODEL_INPUT_H    96
#define MODEL_INPUT_CH   3        // RGB
#define NUM_CLASSES      6

// Normalizacion usada al entrenar: [-1,1] (MobileNet preprocess_input)
//   0 -> [0,255]   1 -> [0,1]   2 -> [-1,1]
#define MODEL_INPUT_NORMALIZATION 2

// Indice de la clase "Saludable" (Tomato___healthy)
#define CLASS_HEALTHY_INDEX 0

// Memoria de trabajo del interprete (en PSRAM)
#define TENSOR_ARENA_SIZE (400 * 1024)

// ---------- Camara ----------
#define CAM_FRAME_SIZE   FRAMESIZE_240X240
#define CAM_FRAME_W      240
#define CAM_FRAME_H      240
#define CAM_WARMUP_FRAMES 3
#define FLASH_LED_PIN    4
#define USE_FLASH_LED    0        // 1 = prende el flash al tomar la foto

// ---------- Ciclo ----------
#define MODO_CONTINUO         0       // 0 = analiza solo al pulsar CAPTURA
                                      // 1 = analiza solo, cada CAPTURE_INTERVAL_MS
#define CAPTURE_INTERVAL_MS   2000    // espera entre fotos (modo continuo)
#define CONFIDENCE_THRESHOLD  0.68f   // umbral recomendado por tu entrenamiento

// ---------- Envio a la pantalla (ESP-NOW) ----------
#define CANAL_ESPNOW     1            // debe ser el mismo que en 2_pantalla_receptor
