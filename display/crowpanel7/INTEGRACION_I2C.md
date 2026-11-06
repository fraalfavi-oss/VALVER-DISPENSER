# VALVER SMART DISPENSER — HMI cableada I2C

Rama de integración en revisión (NO validada aún en hardware). Sin Wi-Fi ni programas adicionales.

## Cableado

- Elecrow conector **I2C SDA** -> ESP32 motores **GPIO11 / SDA**
- Elecrow conector **I2C SCL** -> ESP32 motores **GPIO12 / SCL**
- Elecrow **GND** -> ESP32 motores **GND**
- **NO** unir 3.3 V, 5 V, 24 V entre las placas usando el conector I2C
- Confirmar las funciones en la serigrafía; NO confiar solo en el color del cable

Pantalla ESP32-S3: `Wire1` maestro compartido con GT911 (GPIO19/20).
Motor ESP32-S3: `Wire` esclavo 0x32 (GPIO11/12).
Los dos TMC2209 siguen con sus UART independientes; no se modifican sus pines.

## Recuperar rama directamente desde GitHub

```powershell
cd "C:\Users\franklin.fajarado\VALVER_SMART_DISPENSER"
git status --short
git fetch origin
git switch --track origin/feature/hmi-i2c-cable-final
pio run -d ".\firmware\controller"
pio run -d ".\display\crowpanel7"
```

Si hay cambios locales, conservarlos antes del cambio de rama. No flashear ante errores de compilación.

## Prueba por etapas

1. Con motores SIN 24V, comprobar que ambas compilaciones terminan en SUCCESS.
2. Cerrar monitores serie. Cargar controlador por COM3 y pantalla por COM15, **una placa a la vez**.
3. Alimentar ambas placas por USB, manteniendo la potencia de los motores desconectada.
4. Verificar que el GT911 sigue respondiendo y que la HMI muestra **I2C: CONECTADO 0x32**.
5. Verificar respuestas de ambos TMC2209 (con alimentación de driver adecuada, sin movimiento).
6. Validar mecánica, corriente nominal del NEMA 11, protecciones, soporte ante caída del eje y parada física antes de accionar motores.
7. Probar movimiento supervisado con recorridos libres y sujeción de las agujas.

## Límites

- La parada de pantalla es solo una orden de software. No sustituye una parada de emergencia de hardware.
- StallGuard4 no está validado como detector seguro de fin de carrera ni de contacto con la mesa; la búsqueda automática queda bloqueada.
- Los valores de altura se guardan pero, tras reiniciar, cada posición requiere referencia física de nuevo.
- Si falla la comunicación, el firmware debe detener el movimiento por watchdog; evaluar peligro de caída de eje vertical al perder retención.
- Los cambios de esta rama aún deben compilarse y probarse en las placas reales antes de considerarse terminados.
