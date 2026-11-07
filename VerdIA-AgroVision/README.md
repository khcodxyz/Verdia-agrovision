# VerdIA AgroVisión (DetectaHoja) — Prototipo v1.0

Detección de enfermedades en hojas de tomate con visión artificial en el borde (ESP32-CAM, modelo INT8 en el dispositivo). El diagnóstico se envía por ESP-NOW a una pantalla (ESP32 + OLED). Proyecto UTEC, PI6002. ODS 2: Hambre cero.

## Cómo funciona
1. Se pulsa CAPTURA en la pantalla (GPIO32).
2. La pantalla envía un pedido de 2 bytes ('V','P') por ESP-NOW (canal 1, broadcast) a la cámara.
3. La ESP32-CAM toma la foto, ejecuta VerdIA-Tomato-6 (96×96×3, INT8, 6 clases) y responde con estado, confianza y clase.
4. La OLED muestra el resultado 15 s y vuelve a temperatura y humedad (DHT11). Si no llega respuesta en 8 s muestra "SIN SENAL".

Mensaje ESP-NOW: `{marca[2]='V','A'; estado (0 sana, 1 dudoso, 2 enferma, 3 sin hoja); confianza 0-100; clase[28]}`

## Hardware
| Elemento | Detalle |
|---|---|
| Cámara | ESP32-CAM AI Thinker (OV2640), placa esp32 core 2.0.4 |
| Pantalla | ESP32 Dev Module + OLED SSD1306 128×64 (SDA 21, SCL 22, 0x3C) |
| Botón | GPIO32 a GND (INPUT_PULLUP) |
| Sensor | DHT11 en GPIO16 |
| Alimentación | Power bank 4000 mAh por USB |
| Caja | MDF 3 mm, corte láser, ensamble a presión (`hardware/`) |

## Software
Librerías (Sketch > Include Library > Add .ZIP Library, sin descomprimir): `librerias/Chirale_TensorFlowLite.zip`, `librerias/DHTesp.zip`; además Adafruit GFX y Adafruit SSD1306.

| Carpeta | Uso |
|---|---|
| `firmware/1_prueba_enlace` | Prueba de comunicación cámara-pantalla sin modelo |
| `firmware/2_pantalla_receptor` | Pantalla en uso normal (`BURNIN 0`) |
| `firmware/2_pantalla_burnin` | Pantalla en modo prueba de estrés (`BURNIN 1`) |
| `firmware/3_cam_verdia` | Cámara con el modelo INT8 |

## Prueba de estrés (burn-in)
Subir `2_pantalla_burnin` solo a la pantalla. Cada 10 s pide una captura y cuenta enviados y recibidos; la OLED muestra tiempo, temperatura, máxima, enviados, recibidos y acierto. Cada 60 s imprime por serie una línea `BURNIN,...`.

## Fabricación de la caja
`hardware/plancha_mdf_60x50.dxf` (capa CORTE en rojo, GRABAR en azul) y `.svg`. Plancha de 60×50 cm, MDF 3 mm. Ranuras diseñadas para 3.0 mm nominal; medir el espesor real antes de cortar.

## Modelo
MobileNet V1 (α = 0.25), transfer learning desde ImageNet, dataset de Mendeley Data (DOI 10.17632/3zwdw6y4pn.1), 6 clases, cuantización INT8 (~301.5 KiB). Exactitud del modelo INT8 en el conjunto de prueba (990 imágenes): **79.19 %** (macro-F1 0.789; Keras 80.71 %). Con el umbral de confianza 0.68 del firmware, la exactitud sobre las predicciones aceptadas es 90.2 % (cobertura 68.4 %). Registro: RUN-20260914-095815. Meta de reentrenamiento: ≥ 95 %.

## Documentación (`docs/`)
BOM, diagrama de estados (FSM), esquema electrónico, resultados de validación.

## Versión
v1.0 — entrega de consolidación del prototipo.
