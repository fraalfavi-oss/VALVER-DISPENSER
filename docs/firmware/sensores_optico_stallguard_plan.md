# VALVER · Sensor óptico y StallGuard4 · Preparación de integración

Estado: **DISEÑO / BLOQUEADO**. No es una validación del hardware ni habilita motores.

## Sensor de referencia de punta

Modelo previsto: Omron EE-SX671, fotomicrosensor de ranura 5 mm, alimentación 5–24 V DC, salida NPN y selección de modo Light-ON / Dark-ON.

- No conectar la alimentación de 5–24 V ni una salida de esa tensión directamente a un GPIO ESP32 de 3,3 V.
- La interfaz de salida NPN requerirá pull-up a 3,3 V, referencia GND compatible y validación de cableado y modo de salida, según variante y conector exacto.
- **GPIO óptico sin asignar**. Se elegirá después de verificar placa externa y esquema real.
- La lectura debe tener antirrebote/filtrado, detección de sensor permanentemente activo/inactivo, límites independientes y timeout.
- La calibración real queda bloqueada. Ninguna coordenada Z se acepta desde un sensor no verificado.

## TMC2209 V1.3, StallGuard4

- Pendientes: cable PDN_UART por driver, identificación de configuración de jumper/resistencia BTT V1.3, elección de GPIO UART, estado de DIAG y conexión de alimentación VM.
- StallGuard4 solo se evaluará con el motor correctamente alimentado, parámetros de corriente correctos y movimiento dentro de un rango apropiado. No equivale a medir resistencia de bobina de forma directa ni garantiza detección de contacto suave con la mesa.
- Deben verificarse lectura UART y registros, modo compatible, SGTHRS/SG_RESULT, falsos positivos y velocidad.
- No usar StallGuard4 como único mecanismo anticolisión: son obligatorios topes, límites de recorrido independientes y procedimiento supervisado.
- **GPIO UART y DIAG sin asignar** hasta confirmar el cableado físico.
- Los drivers EN permanecen HIGH; VM y motores desconectados durante pruebas GPIO.

## Futuro movimiento sostenido

Requiere: orden JOG_START, latido periódico del PC, parada por liberación JOG_STOP, watchdog del ESP32, tiempo/distancia máximos por movimiento, límites superiores cableados, fallo con estado latched y rearme manual. Estos comandos se **rechazan** en este firmware mientras falte validar el hardware.

## API de diagnóstico disponible

Enviar NDJSON por USB a 115200 baud:

`{"id":21,"cmd":"GET_SENSORS"}`

Devuelve configurado/conectado=false y lecturas=null hasta instrumentar los sensores. El resultado es **estado de configuración**, no una medición física.

## Confirmaciones pendientes

1. Modelo exacto y fotos del ESP32-S3 externo y sus GPIO.
2. Variante/conector del EE-SX671 y su cableado real, tensión y modo.
3. Cableado real de UART y DIAG para los dos BTT TMC2209 V1.3.
4. Ficha de corriente nominal NEMA11, transmisión Z (husillo/paso) y finales de carrera.
5. Fuente de potencia y comprobación eléctrica antes de habilitar EN/VM.
