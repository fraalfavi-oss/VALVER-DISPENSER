# Interfaz HMI CrowPanel de 7 pulgadas

Proyecto PlatformIO independiente para la pantalla Elecrow CrowPanel ESP32 HMI
de 7 pulgadas (800x480), con ESP32-S3 integrado. El firmware del controlador
principal no se carga en esta placa: la HMI se programa por su propio USB-C.

El diseño recibido se adaptó como una interfaz local de control Z con vistas de
Inicio, calibración de punta, calibración de mesa, prueba Z, diagnóstico y
ajustes. La interfaz está en modo demo: sus controles no envían órdenes a motores.
Detalles y límites de seguridad en [docs/DISENO_UI_INTEGRADO.md](docs/DISENO_UI_INTEGRADO.md).

## Compilar y cargar

Abre esta carpeta (`display`) como proyecto PlatformIO. Conecta solo el CrowPanel
por USB-C, selecciona el nuevo puerto COM y ejecuta `PlatformIO: Build` y luego
`PlatformIO: Upload`. El monitor serie es 115200 baudios.

La primera compilación descarga LVGL, LovyanGFX y el controlador GT911; requiere
conexión a Internet. Si la carga no inicia, usa BOOT según el manual de Elecrow.
No conectes BAT a 24 V.

La interfaz navega entre las seis vistas del diseño y muestra actuadores
bloqueados. Los controles de movimiento y guardado solo presentan avisos demo y
no actúan sobre hardware. La comunicación UART con el controlador principal
queda para el siguiente paso.

La inicialización RGB, GT911 y secuencia del expansor PCA9557 están basadas en la
documentación oficial de Elecrow para la DIS08070H V3.0. El pinout/temporización
de pantalla no debe reutilizarse en otros modelos CrowPanel.
