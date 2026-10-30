# Diseño HMI VALVER integrado

## Fuente de diseño recibida

El archivo recibido `VALVER_HMI_DISENO_LVGL9.zip` contiene una interfaz prototipo LVGL 9.x y una referencia aprobada de 800x480. Su fuente original (`src/valver_ui.c`) depende de LVGL y es deliberadamente solo visual: no inicializa el hardware del CrowPanel ni envía comandos de motores.

La aplicación de pantalla se adaptó a dibujo directo con LovyanGFX porque el render LVGL parcial anterior producía inestabilidad con el transporte RGB. Si se usa LVGL de nuevo, deberá integrarse con el framebuffer y el cambio sincronizado por VSYNC del ejemplo oficial Elecrow. El render directo usa ahora el PCLK RGB de 24 MHz y los porches publicados por Elecrow para la pantalla DIS08070H de 800x480. La secuencia PCA9557 deja IO1 como entrada, según el ejemplo táctil V3. No compilar ambos renderizadores al mismo tiempo.

## Pantallas y navegación

- **Control Z / Inicio:** dos canales lado a lado: Z1 conductivo y Z2 estructural; estado de demostración, posición actual, reposo, dispensación, paso y controles previstos.
- **Calibración de punta:** explicación para referencia óptica EE-SX671 por separado para Z1 y Z2.
- **Calibración de mesa:** pantalla informativa; método de contacto todavía pendiente de validar.
- **Prueba Z:** vista experimental de carga; StallGuard4 no se presenta como sensor de resistencia o contacto seguro.
- **Diagnóstico:** indica que el controlador no tiene enlace de datos.
- **Ajustes Z:** indica que parámetros y límites todavía no están validados.

La barra izquierda navega entre las seis vistas. En Inicio aparecen accesos separados para calibrar la punta Z1, la punta Z2, la mesa Z1 y la mesa Z2. Cada canal tiene botones previstos para subir/bajar, ir a reposo, ir a dispensación y guardar posiciones. Al tocar la zona superior de una tarjeta de canal, se marca el canal seleccionado con un borde verde doble. Las pantallas secundarias muestran acciones simuladas por canal y un botón para volver.

## Función actual de botones y seguridad

Todos los controles son **demostrativos**. Solo cambian la vista o el mensaje inferior; no hay UART conectado ni órdenes a la controladora. No se mueve ningún motor, no se guarda ninguna posición y no se inicia un procedimiento de calibración.

En el controlador principal deben validarse independientemente homing, límites, parada, fallos y permiso de movimiento. La pantalla nunca debe ser autoridad de seguridad. Antes de habilitar botones de movimiento, diseñar el protocolo, las confirmaciones, los enclavamientos y las pruebas de banco.

## Resolución y coordenadas

Canvas horizontal de 800x480. Origen de coordenadas arriba a la izquierda. Los botones de navegación permanecen en la barra lateral izquierda; se mantienen controles amplios apropiados para tacto. El render actual redibuja solo al navegar o cambiar un mensaje; no actualiza la pantalla de forma continua.

## Archivos

- `display/src/ScreenManager.cpp`: dibujo directo LovyanGFX, navegación, regiones táctiles y mensajes demo.
- `display/src/main.cpp`: arranque de la HMI y bucle de actualización táctil.
- `display/platformio.ini`: entorno separado del firmware controlador.

El ZIP recibido sigue siendo la referencia original LVGL. La integración se considera adaptada, no una compilación directa de `valver_ui.c`.
