#pragma once
// ============================================================
// camera_pins.h - Pinout del módulo AI-Thinker ESP32-CAM (OV2640)
// Fuente: ejemplo CameraWebServer del core Arduino-ESP32.
// Si usas otra placa (ESP-EYE, M5Stack, Freenove, XIAO ESP32S3)
// reemplaza este bloque por el correspondiente.
// ============================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// GPIO que quedan libres en AI-Thinker si NO se usa la microSD:
//   2, 4 (LED flash), 12*, 13, 14, 15*, 16 (ocupado por PSRAM en algunas
//   revisiones), 1/3 (UART0, usados por el monitor serial).
//   * = pines de strapping, usar con cuidado.
