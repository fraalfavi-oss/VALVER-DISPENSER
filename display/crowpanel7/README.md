# VALVER SMART DISPENSER - CrowPanel 7" (DIS08070H) touchscreen demo

**Objetivo:** cargar en la pantalla fisica Elecrow una version LVGL 9.1 de la
interfaz VALVER. Es una prueba VISUAL y TACTIL totalmente aislada de los motores.

## Comandos desde VS Code PowerShell (Windows)

Conectar por USB-C solamente la CrowPanel. Desconectar el USB del ESP32 que
controla los TMC2209 para no confundir los puertos COM. No conectar cables UART
entre placas en esta fase.

```powershell
cd "C:\Users\franklin.fajarado\VALVER_SMART_DISPENSER"
git pull origin feature/hmi-pc-usb-safe
pio device list
pio run -d ".\display\crowpanel7"
pio run -d ".\display\crowpanel7" -t upload --upload-port COM7
```

**Sustituir COM7** por el COM que aparezca al enchufar la CrowPanel.
No utilizar automaticamente COM5: podria ser el ESP32 de los motores.

Opcional, para ver que botones se tocan:

```powershell
pio device monitor --port COM7 --baud 115200
```

Cerrar el monitor antes de la siguiente carga, porque bloquea el puerto COM.

## Pantallas incluidas (800 x 480)

- Control Z: Z1 y Z2, SUBIR, BAJAR, IR A REPOSO, DISPENSACION, velocidad.
- Calibrar: Z1 / Z2 / AMBOS; pasos DISPENSACION -> REPOSO (AMBOS confirma Z2).
- Buscar origen: pagina informativa con pulsacion de prueba (sin movimiento).
- Diagnostico: estado del monitor, prueba de tactil y contador de pulsaciones.
- PARADA en barra superior.

**MUY IMPORTANTE:** todos los botones son de simulacion.
No hay UART, movimiento de motores, calibracion fisica, busqueda StallGuard,
ni guardado real de alturas en esta prueba. La logica real sigue en el
ESP32 separado, en firmware/controller. No flashear este firmware en el
ESP32 de los motores: va destinado unicamente al ESP32 integrado de la pantalla.

## Hardware y compatibilidad

Proyecto basado en el ejemplo de **Elecrow V3.0 PlatformIO70**, que usa:
- LVGL 9.1
- LovyanGFX 1.2.25 con parches oficiales Bus_RGB
- LCD RGB 800x480 y GT911 con PCA9557
- ESP32-S3 con 4MB flash / 8MB PSRAM

Fuente del soporte hardware:
https://github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480/tree/master/example/V3.0/PlatformIO70

Si la placa es **V1.x/V2.x** no cargar este firmware hasta adaptar sus pines
y controlador tactil. La revision fisica se puede leer por detras del modulo.
Si la pantalla queda negra o el tactil no funciona, revisar primero
revision V3.0 y el puerto COM, no probar con alimentacion de motores.

Para el siguiente paso se anadira comunicacion UART de 3.3V entre
la CrowPanel y el ESP32 de motores, siempre despues de probar la UI aislada.

## 2026-11-06: reducir parpadeo y activar GT911

- Pixel clock RGB ajustado de 24MHz a 15MHz (temporizacion documentada para V3).
- I2C de GT911 a 100kHz; GPIO19 y GPIO20 liberados del USB Serial/JTAG.
- GT911 se inicia sin escribir al expansor PCA9557 y prueba 0x5D y 0x14.
- Diagnostico: etiqueta TACTIL OK 0x5D / 0x14 o SIN RESPUESTA.
- En DIAGNOSTICO aparece ULTIMO TOQUE X/Y, y en el monitor COM15
  la linea `[TOUCH] X=... Y=...` cuando empieza un toque.
- Si el touch sigue sin respuesta: comprobar revision V3.0 y ver
  `[TOUCH] GT911 no responde...` en serial a 115200, antes de cambiar pines.

Comandos (sin administrador) desde VS Code:
```powershell
cd "C:\Users\franklin.fajarado\VALVER_SMART_DISPENSER"
if (-not (Test-Path "P:\")) { subst P: "$env:USERPROFILE" }
$env:PLATFORMIO_CORE_DIR = "P:\pio"
git pull origin feature/hmi-pc-usb-safe
pio run -d ".\display\crowpanel7" -t upload --upload-port COM15
```
Solo CrowPanel en COM15; cerrar monitor serie antes de cargar.
Si la pantalla sigue parpadeando, verificar alimentacion 5V y cable USB.
