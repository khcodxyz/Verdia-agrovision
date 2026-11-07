// ============================================================
// VerdIA - ESP32-CAM con TU modelo Tomato-6 (TensorFlow Lite Micro)
// Basado en tu firmware DetectaHoja_EdgeAI, adaptado a Arduino IDE:
//   - sin WiFi/servidor web
//   - envia el resultado al ESP32 de la pantalla por ESP-NOW
//
// Placa:     "AI Thinker ESP32-CAM"  (soporte esp32 version 2.0.4)
// Libreria:  "Chirale_TensorFlowLite" 2.0.0 (Gestor de librerias)
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include "esp_camera.h"
#include "config.h"
#include "camera_pins.h"
#include "image_utils.h"
#include "inference.h"

// Mensaje: IGUAL que en 2_pantalla_receptor y 1_prueba_enlace
typedef struct __attribute__((packed)) {
  char marca[2];      // siempre 'V','A'
  uint8_t estado;     // 0 sana, 1 dudoso, 2 enferma, 3 sin hoja
  uint8_t confianza;  // 0 a 100
  char clase[28];     // nombre de la clase
} MensajeVerdIA;

static uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static volatile bool g_pedido = false;   // la pantalla pidio una captura

static void recibido(const uint8_t* data, int len) {
  if (len >= 2 && data[0] == 'V' && data[1] == 'P') g_pedido = true;
}
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void alRecibir(const esp_now_recv_info_t* info, const uint8_t* data, int len) { recibido(data, len); }
#else
static void alRecibir(const uint8_t* mac, const uint8_t* data, int len) { recibido(data, len); }
#endif

// ------------------------------------------------------------ camara
static bool camera_init() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0 = Y2_GPIO_NUM;  cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM;  cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM;  cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM;  cfg.pin_d7 = Y9_GPIO_NUM;
  cfg.pin_xclk  = XCLK_GPIO_NUM;
  cfg.pin_pclk  = PCLK_GPIO_NUM;
  cfg.pin_vsync = VSYNC_GPIO_NUM;
  cfg.pin_href  = HREF_GPIO_NUM;
  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn  = PWDN_GPIO_NUM;
  cfg.pin_reset = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_RGB565;      // sin JPEG: pixeles directos
  cfg.frame_size   = CAM_FRAME_SIZE;
  cfg.fb_count     = 1;
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;
  cfg.grab_mode    = CAMERA_GRAB_LATEST;
  cfg.jpeg_quality = 12;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[CAM] esp_camera_init fallo 0x%x\n", err);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
  }
  for (int i = 0; i < CAM_WARMUP_FRAMES; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(100);
  }
  return true;
}

// ------------------------------------------------------------ ESP-NOW
static void espnow_init() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(CANAL_ESPNOW, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] ESP-NOW no inicio");
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(alRecibir);
}

static void enviar(uint8_t estado, float confianza, const char* clase) {
  MensajeVerdIA m = {};
  m.marca[0] = 'V';
  m.marca[1] = 'A';
  m.estado = estado;
  float c = confianza < 0 ? 0 : (confianza > 1 ? 1 : confianza);
  m.confianza = (uint8_t)(c * 100.0f + 0.5f);
  strncpy(m.clase, clase, sizeof(m.clase) - 1);
  esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&m, sizeof(m));
  Serial.printf("[NOW] enviado estado %u (%s %u%%) %s\n", estado, m.clase, m.confianza,
                r == ESP_OK ? "OK" : "ERROR");
}

// ------------------------------------------------------------ ciclo
static bool ciclo() {
#if USE_FLASH_LED
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(150);
#endif
  camera_fb_t* fb = esp_camera_fb_get();      // descarta una foto vieja
  if (fb) esp_camera_fb_return(fb);
  fb = nullptr;
  for (int intento = 1; intento <= 3 && !fb; ++intento) {
    fb = esp_camera_fb_get();
    if (!fb) delay(120);
  }
#if USE_FLASH_LED
  digitalWrite(FLASH_LED_PIN, LOW);
#endif
  if (!fb) { Serial.println("[CAM] Captura fallida"); return false; }

  bool ok = preprocess_rgb565_to_int8(fb->buf, fb->width, fb->height,
                                      inference_input_buffer(),
                                      MODEL_INPUT_W, MODEL_INPUT_H, MODEL_INPUT_CH,
                                      inference_input_scale(), inference_input_zero_point());
  esp_camera_fb_return(fb);
  if (!ok) { Serial.println("[IMG] Preprocesado fallo"); return false; }

  InferenceResult inf;
  if (!inference_run(inf)) return false;

  uint8_t estado;
  if (inf.topScore < CONFIDENCE_THRESHOLD)      estado = 1;   // dudoso
  else if (inf.topIndex == CLASS_HEALTHY_INDEX) estado = 0;   // sana
  else                                          estado = 2;   // enferma

  Serial.println("---------------------------------------------");
  for (int i = 0; i < NUM_CLASSES; ++i) {
    Serial.printf("  %-18s %6.2f %%%s\n", kClassLabels[i], inf.scores[i] * 100.0f,
                  (i == inf.topIndex) ? "  <--" : "");
  }
  Serial.printf("  Inferencia: %lu ms\n", (unsigned long)inf.inferenceTimeMs);

  const char* etiqueta = (inf.topIndex >= 0 && inf.topIndex < NUM_CLASSES) ? kClassLabels[inf.topIndex] : "?";
  enviar(estado, inf.topScore, etiqueta);
  return true;
}

static void tareaInferencia(void*) {
  for (;;) {
#if MODO_CONTINUO
    ciclo();
    vTaskDelay(pdMS_TO_TICKS(CAPTURE_INTERVAL_MS));
#else
    if (g_pedido) {
      g_pedido = false;
      Serial.println("[BTN] Pedido de captura recibido");
      ciclo();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
#endif
  }
}

// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== VerdIA ESP32-CAM ===");
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!psramFound()) {
    Serial.println("[SYS] PSRAM no detectada. Placa: AI Thinker ESP32-CAM.");
    while (true) delay(1000);
  }
  if (!camera_init())    { Serial.println("[SYS] Sin camara. Revisa el cable plano."); while (true) delay(1000); }
  if (!inference_init()) { Serial.println("[SYS] Sin modelo. Detenido.");               while (true) delay(1000); }
  espnow_init();

  // Pila grande: algunos kernels de TFLM usan buffers en la pila
  xTaskCreatePinnedToCore(tareaInferencia, "infer", 16 * 1024, nullptr, 2, nullptr, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
