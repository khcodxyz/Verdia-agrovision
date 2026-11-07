// VerdIA - PRUEBA DE ENLACE (va en el ESP32-CAM)
// No usa la camara ni el modelo: manda resultados FALSOS cada 2 s
// para comprobar que el ESP32 de la pantalla los recibe.
// Placa en Arduino IDE: "AI Thinker ESP32-CAM"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define CANAL 1   // debe ser el mismo en los dos ESP32

// Mensaje: IGUAL en los tres sketches
typedef struct __attribute__((packed)) {
  char marca[2];      // siempre 'V','A'
  uint8_t estado;     // 0 sana, 1 dudoso, 2 enferma, 3 sin hoja
  uint8_t confianza;  // 0 a 100
  char clase[28];     // nombre de la clase del modelo
} MensajeVerdIA;

uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void iniciarEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(CANAL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW no inicio");
    while (true) delay(1000);
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 0;      // 0 = canal actual
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ERROR: no pude agregar el destino");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  iniciarEspNow();
  Serial.println("Prueba de enlace lista. Enviando cada 2 s...");
}

void loop() {
  static uint8_t n = 0;
  const uint8_t estados[4] = {0, 1, 2, 3};
  const uint8_t confianzas[4] = {92, 55, 88, 70};
  const char* clases[4] = {"prueba_sana", "prueba_dudosa", "prueba_enferma", "prueba_fondo"};

  MensajeVerdIA m = {};
  m.marca[0] = 'V';
  m.marca[1] = 'A';
  m.estado = estados[n % 4];
  m.confianza = confianzas[n % 4];
  strncpy(m.clase, clases[n % 4], sizeof(m.clase) - 1);

  esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&m, sizeof(m));
  Serial.printf("Enviado: %s %u%% -> %s\n", m.clase, m.confianza, r == ESP_OK ? "OK" : "ERROR");
  n++;
  delay(2000);
}
