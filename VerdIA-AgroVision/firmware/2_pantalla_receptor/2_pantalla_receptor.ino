// VerdIA - PANTALLA (va en el ESP32 normal de la protoboard)
// Recibe el resultado del ESP32-CAM por ESP-NOW (sin WiFi ni PC)
// y lo muestra en la OLED y en los LEDs.
//   LED verde = SANA, amarillo = DUDOSO, rojo = ALERTA (enferma)
// Boton CAPTURA (GPIO32 a GND): pide a la camara que analice una hoja.
// Sensor: DHT11 (GPIO16)
// Libreria extra: "DHT sensor library for ESPx" (DHTesp)
// Placa en Arduino IDE: "ESP32 Dev Module"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>

#define CANAL 1                 // debe ser el mismo en los dos ESP32

#define SDA_PIN 21
#define SCL_PIN 22
#define LED_ROJO 25
#define LED_AMARILLO 26
#define LED_VERDE 27
#define BOTON 32                // boton CAPTURA: un lado a GPIO32, el otro a GND
#define ESPERA_RESPUESTA_MS 8000
#define RESULTADO_MS 15000      // el resultado se queda 15 s y vuelve a los sensores

#define PIN_DHT 16              // DHT11 DATA

// ---- PRUEBA BURN-IN (2 horas) ----
// 1 = la pantalla pide sola una captura cada BURNIN_PERIODO_MS y cuenta cuantas
//     respuestas llegan. Muestra tiempo, temperatura, enviados, recibidos y acierto.
// 0 = uso normal con el boton CAPTURA.
#define BURNIN 0
#define BURNIN_PERIODO_MS 10000   // un pedido cada 10 s
#define BURNIN_ESPERA_MS 3000     // si en 3 s no llega, cuenta como perdido

// Mensaje: IGUAL en los tres sketches
typedef struct __attribute__((packed)) {
  char marca[2];      // siempre 'V','A'
  uint8_t estado;     // 0 sana, 1 dudoso, 2 enferma, 3 sin hoja
  uint8_t confianza;  // 0 a 100
  char clase[28];     // nombre de la clase del modelo
} MensajeVerdIA;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHTesp dht;
uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

MensajeVerdIA recibido;
volatile bool hayNuevo = false;
volatile unsigned long ultimoMs = 0;
portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;

void guardar(const uint8_t* data, int len) {
  if (len != (int)sizeof(MensajeVerdIA) || data[0] != 'V' || data[1] != 'A') return;
  portENTER_CRITICAL(&candado);
  memcpy(&recibido, data, sizeof(recibido));
  hayNuevo = true;
  ultimoMs = millis();
  portEXIT_CRITICAL(&candado);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void alRecibir(const esp_now_recv_info_t* info, const uint8_t* data, int len) { guardar(data, len); }
#else
void alRecibir(const uint8_t* mac, const uint8_t* data, int len) { guardar(data, len); }
#endif

void leds(bool r, bool a, bool v) {
  digitalWrite(LED_ROJO, r);
  digitalWrite(LED_AMARILLO, a);
  digitalWrite(LED_VERDE, v);
}

void mostrar(const char* titulo, const char* l2, const char* l3) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(titulo);
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.println(l2);
  display.setCursor(0, 54);
  display.println(l3);
  display.display();
}

// "Tomato___Late_blight" -> "Tomato Late blight"
void limpiarNombre(const char* in, char* out, size_t n) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j < n - 1; i++) {
    char c = in[i] == '_' ? ' ' : in[i];
    if (c == ' ' && j > 0 && out[j - 1] == ' ') continue;
    out[j++] = c;
  }
  out[j] = 0;
}

// ------------------------------------------------------------ sensores
float g_temp = NAN, g_hum = NAN;

void leerSensores() {
  TempAndHumidity th = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE) { g_temp = th.temperature; g_hum = th.humidity; }
  Serial.printf("[SENS] T=%.0fC HR=%.0f%%\n", g_temp, g_hum);
}

void mostrarSensores() {
  char l1[24], l2[24];
  const char* l3 = "Pulsa CAPTURA";
  if (isnan(g_temp)) { snprintf(l1, sizeof(l1), "Temp: --"); snprintf(l2, sizeof(l2), "Humedad: --"); }
  else { snprintf(l1, sizeof(l1), "Temp: %.0f C", g_temp); snprintf(l2, sizeof(l2), "Humedad: %.0f %%", g_hum); }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("VerdIA - ambiente");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println(l1);
  display.setCursor(0, 30);
  display.println(l2);
  display.setCursor(0, 50);
  display.println(l3);
  display.display();
}

#if BURNIN
void burninLoop() {
  static unsigned long ultimoPedido = 0, ultimoSens = 0, ultimoLog = 0, ultimoDibujo = 0;
  static unsigned long enviados = 0, recibidos = 0, pedidoMs = 0;
  static bool pendiente = false;
  static float tMax = -100;
  static bool primera = true;
  unsigned long ahora = millis();

  if (primera || ahora - ultimoPedido >= BURNIN_PERIODO_MS) {
    primera = false;
    ultimoPedido = ahora;
    const uint8_t pedido[2] = {'V', 'P'};
    esp_now_send(BROADCAST, pedido, sizeof(pedido));
    enviados++;
    pendiente = true;
    pedidoMs = ahora;
  }

  if (hayNuevo) {
    portENTER_CRITICAL(&candado);
    hayNuevo = false;
    portEXIT_CRITICAL(&candado);
    if (pendiente) { recibidos++; pendiente = false; }
  }
  if (pendiente && ahora - pedidoMs > BURNIN_ESPERA_MS) pendiente = false;   // perdido

  if (ahora - ultimoSens > 2000) {
    ultimoSens = ahora;
    leerSensores();
    if (!isnan(g_temp) && g_temp > tMax) tMax = g_temp;
  }

  unsigned long resueltos = enviados - (pendiente ? 1 : 0);
  float acierto = resueltos > 0 ? 100.0f * recibidos / resueltos : 100.0f;

  if (ahora - ultimoDibujo > 1000) {
    ultimoDibujo = ahora;
    unsigned long s = ahora / 1000;
    char l0[24], l1[24], l2[24], l3[24], l4[24];
    snprintf(l0, sizeof(l0), "BURN-IN %02lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
    if (isnan(g_temp)) snprintf(l1, sizeof(l1), "T:-- HR:--");
    else snprintf(l1, sizeof(l1), "T:%.0fC HR:%.0f%% max:%.0fC", g_temp, g_hum, tMax);
    snprintf(l2, sizeof(l2), "Enviados:  %lu", resueltos);
    snprintf(l3, sizeof(l3), "Recibidos: %lu", recibidos);
    snprintf(l4, sizeof(l4), "Acierto: %.1f%%", acierto);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);  display.println(l0);
    display.setCursor(0, 12); display.println(l1);
    display.setCursor(0, 24); display.println(l2);
    display.setCursor(0, 36); display.println(l3);
    display.setCursor(0, 48); display.println(l4);
    display.display();
  }

  if (ahora - ultimoLog >= 60000) {
    ultimoLog = ahora;
    Serial.printf("BURNIN,min=%lu,T=%.0f,HR=%.0f,Tmax=%.0f,enviados=%lu,recibidos=%lu,acierto=%.1f\n",
                  ahora / 60000, g_temp, g_hum, tMax, resueltos, recibidos, acierto);
  }
  delay(20);
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  leds(0, 0, 0);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("No encuentro la pantalla");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(CANAL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    mostrar("ERROR", "ESP-NOW no inicio", "");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(alRecibir);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  pinMode(BOTON, INPUT_PULLUP);
  dht.setup(PIN_DHT, DHTesp::DHT11);

  leerSensores();
  mostrarSensores();
  Serial.println("Pantalla lista, esperando mensajes");
}

void loop() {
#if BURNIN
  burninLoop();
  return;
#endif
  static bool esperando = false;
  static unsigned long pedidoMs = 0;
  static int ultimoBoton = HIGH;
  static unsigned long resultadoMs = 0;     // 0 = mostrando sensores
  static unsigned long sensoresMs = 0;

  // Boton CAPTURA (con antirrebote simple)
  int b = digitalRead(BOTON);
  if (b == LOW && ultimoBoton == HIGH) {
    delay(30);
    if (digitalRead(BOTON) == LOW) {
      const uint8_t pedido[2] = {'V', 'P'};
      esp_now_send(BROADCAST, pedido, sizeof(pedido));
      leds(0, 0, 0);
      mostrar("ANALIZANDO", "No muevas la hoja", "");
      Serial.println("Boton: pedido de captura enviado");
      esperando = true;
      pedidoMs = millis();
      resultadoMs = millis();
    }
  }
  ultimoBoton = b;

  if (esperando && !hayNuevo && millis() - pedidoMs > ESPERA_RESPUESTA_MS) {
    mostrar("SIN SENAL", "La camara no respondio", "Vuelve a pulsar");
    esperando = false;
    resultadoMs = millis();
  }

  if (hayNuevo) {
    MensajeVerdIA m;
    portENTER_CRITICAL(&candado);
    memcpy(&m, &recibido, sizeof(m));
    hayNuevo = false;
    portEXIT_CRITICAL(&candado);
    m.clase[sizeof(m.clase) - 1] = 0;

    char nombre[32];
    char linea3[24];
    limpiarNombre(m.clase, nombre, sizeof(nombre));
    snprintf(linea3, sizeof(linea3), "Confianza: %u%%", m.confianza);
    Serial.printf("Recibido: estado %u, %s, %u%%\n", m.estado, m.clase, m.confianza);

    switch (m.estado) {
      case 0:  leds(0, 0, 1); mostrar("SANA", nombre, linea3); break;
      case 1:  leds(0, 1, 0); mostrar("DUDOSO", nombre, linea3); break;
      case 2:  leds(1, 0, 0); mostrar("ALERTA", nombre, linea3); break;
      default: leds(0, 0, 0); mostrar("SIN HOJA", "Acerca una hoja", linea3); break;
    }
    esperando = false;
    resultadoMs = millis();
  }

  // Sensores cada 2 s; la pantalla de sensores se muestra cuando no hay resultado
  if (millis() - sensoresMs > 2000) {
    sensoresMs = millis();
    leerSensores();
    if (resultadoMs != 0 && !esperando && millis() - resultadoMs > RESULTADO_MS) {
      resultadoMs = 0;
      leds(0, 0, 0);
    }
    if (resultadoMs == 0) mostrarSensores();
  }

  delay(20);
}
