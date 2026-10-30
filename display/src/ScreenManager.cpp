#include "ScreenManager.h"

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <TAMC_GT911.h>

namespace {
constexpr int32_t kScreenWidth = 800;
constexpr int32_t kScreenHeight = 480;
constexpr int kBacklightPin = 2;
constexpr uint8_t kPca9557Address = 0x18;
constexpr uint8_t kPca9557OutputRegister = 0x01;
constexpr uint8_t kPca9557ConfigurationRegister = 0x03;
constexpr uint16_t kBackground = 0x10A2;
constexpr uint16_t kPanel = 0x1926;
constexpr uint16_t kPanel2 = 0x2189;
constexpr uint16_t kMuted = 0x9E3D;
constexpr uint16_t kBlue = 0x259F;
constexpr uint16_t kRed = 0xF9AA;
constexpr uint16_t kGreen = 0x56D4;
constexpr uint16_t kAmber = 0xF5A8;

enum class UiPage : uint8_t { Home, Tip, Table, Test, Diagnostics, Settings };
UiPage currentPage = UiPage::Home;
uint8_t selectedChannel = 0;
char statusNotice[72] = "SELECCIONA Z1 O Z2";

class CrowPanelDisplay final : public lgfx::LGFX_Device {
public:
  CrowPanelDisplay() {
    auto busConfig = bus_.config();
    busConfig.panel = &panel_;
    const int8_t dataPins[16] = {15, 7, 6, 5, 4, 9, 46, 3,
                                 8, 16, 1, 14, 21, 47, 48, 45};
    memcpy(busConfig.pin_data, dataPins, sizeof(dataPins));
    busConfig.pin_henable = 41;
    busConfig.pin_vsync = 40;
    busConfig.pin_hsync = 39;
    busConfig.pin_pclk = 0;
    // Elecrow's current 7-inch V3 RGB timing uses a 24 MHz pixel clock.
    busConfig.freq_write = 24000000;
    busConfig.hsync_polarity = 0;
    busConfig.hsync_front_porch = 40;
    busConfig.hsync_pulse_width = 48;
    busConfig.hsync_back_porch = 40;
    busConfig.vsync_polarity = 0;
    busConfig.vsync_front_porch = 1;
    busConfig.vsync_pulse_width = 31;
    busConfig.vsync_back_porch = 13;
    busConfig.pclk_active_neg = 1;
    busConfig.de_idle_high = 0;
    busConfig.pclk_idle_high = 0;
    bus_.config(busConfig);

    auto panelConfig = panel_.config();
    panelConfig.memory_width = kScreenWidth;
    panelConfig.memory_height = kScreenHeight;
    panelConfig.panel_width = kScreenWidth;
    panelConfig.panel_height = kScreenHeight;
    panelConfig.offset_x = 0;
    panelConfig.offset_y = 0;
    panel_.config(panelConfig);
    panel_.setBus(&bus_);
    setPanel(&panel_);
  }

private:
  lgfx::Bus_RGB bus_;
  lgfx::Panel_RGB panel_;
};

CrowPanelDisplay display;
TAMC_GT911 touch(19, 20, -1, -1, kScreenWidth, kScreenHeight);
bool previousTouch = false;
uint32_t lastTouchPoll = 0;
uint32_t lastTouchAction = 0;

bool writePca9557(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kPca9557Address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool initializeBoardExpander() {
  // Elecrow V3: drive IO0/IO1 low, release IO0 high, then set IO1 as input.
  if (!writePca9557(kPca9557ConfigurationRegister, 0xFC) ||
      !writePca9557(kPca9557OutputRegister, 0x00)) {
    return false;
  }
  delay(20);
  if (!writePca9557(kPca9557OutputRegister, 0x01)) {
    return false;
  }
  delay(100);
  // PCA9557 bits are 0=output, 1=input: keep IO0 output, release IO1 input.
  return writePca9557(kPca9557ConfigurationRegister, 0xFD);
}

uint8_t detectGt911Address() {
  for (uint8_t address : {uint8_t{0x5D}, uint8_t{0x14}}) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      return address;
    }
  }
  return 0;
}

uint16_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
  return display.color565(red, green, blue);
}

void textAt(const char* text, int32_t x, int32_t y, uint8_t size,
            uint16_t color) {
  display.setTextSize(size);
  display.setTextColor(color);
  display.setCursor(x, y);
  display.print(text);
}

void card(int32_t x, int32_t y, int32_t w, int32_t h) {
  display.fillRoundRect(x, y, w, h, 16, kPanel);
}

void button(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill,
            const char* label) {
  display.fillRoundRect(x, y, w, h, 12, fill);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE);
  display.setTextDatum(MC_DATUM);
  display.drawString(label, x + w / 2, y + h / 2);
  display.setTextDatum(TL_DATUM);
}

void drawHeader(const char* pageName) {
  display.fillRect(0, 0, kScreenWidth, 46, rgb(7, 21, 37));
  textAt("VALVER", 14, 7, 3, TFT_WHITE);
  display.drawFastVLine(202, 5, 36, rgb(21, 139, 255));
  textAt("SMART DISPENSER", 220, 5, 1, rgb(220, 231, 245));
  display.setTextDatum(TL_DATUM);
  display.setTextSize(1);
  display.setTextColor(rgb(220, 231, 245));
  display.drawString(pageName, 452, 16);
  display.fillRoundRect(626, 6, 164, 34, 7, rgb(73, 24, 42));
  textAt("CONTROL", 636, 8, 1, rgb(255, 190, 198));
  textAt("SIN ENLACE", 636, 21, 2, rgb(255, 106, 125));
  display.setTextDatum(TL_DATUM);
}

void drawShell(const char* title) {
  display.fillScreen(kBackground);
  drawHeader(title);
  card(8, 54, 108, 376);
  button(14, 61, 96, 42, kPanel, "INICIO");
  button(14, 111, 96, 42, kPanel, "PUNTA Z");
  button(14, 161, 96, 42, kPanel, "MESA Z");
  button(14, 211, 96, 42, kPanel, "PRUEBA Z");
  button(14, 261, 96, 42, kPanel, "DIAGN.");
  button(14, 311, 96, 42, kPanel, "AJUSTES");
  display.fillRoundRect(10, 365, 104, 57, 8, rgb(91, 24, 42));
  textAt("MOTORES", 24, 375, 1, rgb(255, 170, 180));
  textAt("DESHABILITADOS", 16, 391, 1, rgb(255, 170, 180));
  display.fillRect(126, 450, 666, 20, kBackground);
  textAt(statusNotice, 133, 454, 1, rgb(255, 170, 180));
}

void drawChannel(int channel, int x) {
    const uint16_t channelColor = channel == 0 ? kBlue : kRed;
    const uint16_t accent = selectedChannel == channel + 1 ? kGreen : channelColor;
  card(x, 54, 326, 270);
  display.drawRoundRect(x, 54, 326, 270, 10, accent);
    if (selectedChannel == channel + 1) {
      display.drawRoundRect(x + 2, 56, 322, 266, 9, rgb(190, 255, 232));
    }
  display.fillRoundRect(x + 8, 62, 12, 27, 5, accent);
  textAt(channel == 0 ? "Z1" : "Z2", x + 30, 60, 2, TFT_WHITE);
  textAt(channel == 0 ? "Adhesivo conductivo" : "Adhesivo estructural",
         x + 30, 80, 1, kMuted);
    display.fillCircle(x + 299, 74, 4, kAmber);
    textAt(selectedChannel == channel + 1 ? "SELECCIONADA" : "TOCAR",
           x + 258, 82, 1, kAmber);

  textAt("Posicion Z actual", x + 12, 103, 1, kMuted);
  textAt("--.--- mm", x + 12, 119, 2, TFT_WHITE);
  textAt("Paso de movimiento", x + 190, 103, 1, kMuted);
  textAt("Pendiente", x + 190, 119, 2, TFT_WHITE);
  display.drawFastHLine(x + 10, 146, 306, rgb(35, 67, 94));
  textAt("Altura reposo", x + 12, 153, 1, kMuted);
  textAt("--.--- mm", x + 12, 169, 2, TFT_WHITE);
  textAt("Altura dispensacion", x + 12, 195, 1, kMuted);
  textAt("--.--- mm", x + 12, 211, 2, TFT_WHITE);

  button(x + 169, 151, 145, 39, kPanel2, "Ir a reposo");
  button(x + 169, 197, 145, 39, kPanel2, "Ir a dispens.");
  button(x + 9, 247, 148, 34, kPanel2, "Subir");
  button(x + 169, 247, 145, 34, kPanel2, "Bajar");
    button(x + 9, 286, 305, 29, channel == 0 ? 0x0750A0 : 0x78253B,
      "Guardar posiciones");
}

void drawHome() {
  currentPage = UiPage::Home;
  drawShell("CONTROL Z - DOS AGUJAS");
  drawChannel(0, 125);
  drawChannel(1, 462);

  card(125, 333, 326, 105);
  textAt("CALIBRACION DE PUNTA DE AGUJA", 137, 342, 1, TFT_WHITE);
  textAt("Referencia individual por sensor optico", 137, 361, 1, kMuted);
  button(137, 383, 143, 40, kBlue, "Calibrar Z1");
  button(289, 383, 151, 40, kRed, "Calibrar Z2");

  card(462, 333, 326, 105);
  textAt("CALIBRACION DE ALTURA DE MESA", 474, 342, 1, TFT_WHITE);
  textAt("Solo tras validar sensor y procedimiento", 474, 361, 1, kMuted);
  button(474, 383, 143, 40, kBlue, "Mesa Z1");
  button(626, 383, 151, 40, kRed, "Mesa Z2");
    textAt("Posiciones y sensores: pendientes de conexion", 526, 31, 1,
      kMuted);
}

void drawDetail(UiPage page) {
  const char* title = "DIAGNOSTICO";
  const char* heading = "Comunicacion no configurada";
  const char* detail = "La HMI solo muestra la interfaz. No hay telemetria ni "
                       "motores conectados.";
  switch (page) {
    case UiPage::Tip:
      title = "CALIBRACION DE PUNTA";
      heading = selectedChannel == 1 ? "Referencia optica EE-SX671 - Z1"
                                     : selectedChannel == 2 ? "Referencia optica EE-SX671 - Z2"
                                                            : "Referencia optica EE-SX671";
      detail = "El procedimiento de busqueda de punta aun no esta conectado. "
               "Z1 y Z2 deben calibrarse por separado.";
      break;
    case UiPage::Table:
      title = "CALIBRACION DE MESA";
      heading = selectedChannel == 1 ? "Altura de superficie - Z1"
                                     : selectedChannel == 2 ? "Altura de superficie - Z2"
                                                            : "Altura de superficie";
      detail = "Metodo y sensor de contacto pendientes de validar. "
               "StallGuard4 no se toma como medicion de contacto segura.";
      break;
    case UiPage::Test:
      title = "PRUEBA Z - EXPERIMENTAL";
      heading = "Prueba de carga del eje";
      detail = "StallGuard4 puede estimar carga. No mide resistencia de bobina "
               "ni confirma por si solo un contacto seguro.";
      break;
    case UiPage::Settings:
      title = "AJUSTES Z";
      heading = "Parametros pendientes";
      detail = "Paso mecanico, limites y velocidades requieren valores de "
               "hardware aprobados antes de guardarse.";
      break;
    case UiPage::Diagnostics:
      break;
    case UiPage::Home:
      drawHome();
      return;
  }

  currentPage = page;
  drawShell(title);
  card(136, 65, 646, 365);
  textAt(heading, 158, 91, 2, TFT_WHITE);
  textAt(detail, 158, 135, 1, kMuted);
  textAt("Z1 - CONDUCTIVO", 158, 190, 1, kBlue);
  textAt("Z2 - ESTRUCTURAL", 470, 190, 1, kRed);
    display.drawRoundRect(150, 180, 294, 101, 9,
           selectedChannel == 1 ? kGreen : kBlue);
    display.drawRoundRect(462, 180, 294, 101, 9,
           selectedChannel == 2 ? kGreen : kRed);
    button(158, 226, 280, 54,
      selectedChannel == 1 ? kGreen : kPanel2, "Seleccionar Z1");
    button(470, 226, 280, 54,
      selectedChannel == 2 ? kGreen : kPanel2, "Seleccionar Z2");
  textAt("Sin ordenes enviadas; salidas permanecen inhibidas.", 158, 305, 1,
         kAmber);
  button(295, 357, 330, 54, kBlue, "Volver al control Z");
}

void showStatusNotice(const char* message) {
  snprintf(statusNotice, sizeof(statusNotice), "%s", message);
  display.fillRect(126, 450, 666, 20, kBackground);
  textAt(statusNotice, 133, 454, 1, rgb(255, 170, 180));
}

bool handleTouch(int32_t x, int32_t y) {
  if (x < 116 && y >= 54 && y <= 353) {
    if (y < 103) drawHome();
    else if (y < 153) drawDetail(UiPage::Tip);
    else if (y < 203) drawDetail(UiPage::Table);
    else if (y < 253) drawDetail(UiPage::Test);
    else if (y < 303) drawDetail(UiPage::Diagnostics);
    else drawDetail(UiPage::Settings);
    return true;
  }

  if (currentPage == UiPage::Home) {
    if (y >= 383 && y <= 423 && x >= 137 && x <= 280) {
      selectedChannel = 1;
      drawDetail(UiPage::Tip);
      return true;
    }
    if (y >= 383 && y <= 423 && x >= 289 && x <= 440) {
      selectedChannel = 2;
      drawDetail(UiPage::Tip);
      return true;
    }
    if (y >= 383 && y <= 423 && x >= 474 && x <= 617) {
      selectedChannel = 1;
      drawDetail(UiPage::Table);
      return true;
    }
    if (y >= 383 && y <= 423 && x >= 626 && x <= 777) {
      selectedChannel = 2;
      drawDetail(UiPage::Table);
      return true;
    }
    if (y >= 151 && y <= 236 &&
        ((x >= 294 && x <= 439) || (x >= 631 && x <= 776))) {
      showStatusNotice("ORDEN NO ENVIADA: controladora sin conexion");
      return true;
    }
    if (y >= 247 && y <= 281 &&
        ((x >= 134 && x <= 282) || (x >= 294 && x <= 439) ||
         (x >= 471 && x <= 619) || (x >= 631 && x <= 776))) {
      showStatusNotice("MOVIMIENTO BLOQUEADO: falta homing y seguridad");
      return true;
    }
    if (y >= 286 && y <= 315 &&
        ((x >= 134 && x <= 439) || (x >= 471 && x <= 776))) {
      showStatusNotice("GUARDADO NO DISPONIBLE: controladora sin conexion");
      return true;
    }
    if (y >= 54 && y <= 324 && x >= 125 && x <= 451) {
      selectedChannel = 1;
      snprintf(statusNotice, sizeof(statusNotice), "Z1 SELECCIONADA");
      drawHome();
      return true;
    }
    if (y >= 54 && y <= 324 && x >= 462 && x <= 788) {
      selectedChannel = 2;
      snprintf(statusNotice, sizeof(statusNotice), "Z2 SELECCIONADA");
      drawHome();
      return true;
    }
  } else if (y >= 357 && y <= 411 && x >= 295 && x <= 625) {
    drawHome();
    return true;
  } else if (y >= 226 && y <= 280 && ((x >= 158 && x <= 438) ||
                                      (x >= 470 && x <= 750))) {
    selectedChannel = x < 470 ? 1 : 2;
    snprintf(statusNotice, sizeof(statusNotice), "%s SELECCIONADA",
         selectedChannel == 1 ? "Z1" : "Z2");
    drawDetail(currentPage);
    return true;
    return true;
  }
  return false;
}
}  // namespace

namespace valver {
ScreenManager::ScreenManager() = default;

bool ScreenManager::initialize() {
  Serial.printf("PSRAM total: %lu bytes\n",
                static_cast<unsigned long>(ESP.getPsramSize()));
  Wire.begin(19, 20);
  if (!initializeBoardExpander()) {
    Serial.println("HMI: PCA9557 not found or reset failed");
    return false;
  }
  Serial.println("HMI: PCA9557 ready");

  if (!display.begin()) {
    Serial.println("HMI: LCD init failed; check OPI PSRAM configuration");
    return false;
  }
  Serial.printf("HMI: RGB LCD ready (%ld x %ld)\n",
                static_cast<long>(display.width()),
                static_cast<long>(display.height()));
  const uint8_t gt911Address = detectGt911Address();
  if (gt911Address == 0) {
    Serial.println("HMI: no GT911 ACK at I2C 0x5D or 0x14");
    touch.begin();
  } else {
    Serial.printf("HMI: GT911 ACK at I2C 0x%02X\n", gt911Address);
    touch.begin(gt911Address);
  }
  touch.setRotation(ROTATION_NORMAL);
  Serial.println("HMI: GT911 touch ready");

  pinMode(kBacklightPin, OUTPUT);
  digitalWrite(kBacklightPin, LOW);
  delay(300);
  digitalWrite(kBacklightPin, HIGH);

  drawHome();
  Serial.println("HMI: RGB display and GT911 touch initialized");
  initialized_ = true;
  return true;
}

void ScreenManager::update() {
  if (!initialized_ || millis() - lastTouchPoll < 20) {
    return;
  }
  lastTouchPoll = millis();
  touch.read();
  const bool isPressed = touch.isTouched;
  if (isPressed && !previousTouch && millis() - lastTouchAction > 250) {
    const int32_t x = touch.points[0].x;
    const int32_t y = touch.points[0].y;
    Serial.printf("TOUCH: pressed=%d points=%u x=%ld y=%ld\n",
                  static_cast<int>(touch.isTouched), touch.touches,
                  static_cast<long>(x), static_cast<long>(y));
    if (x >= 0 && x < kScreenWidth && y >= 0 && y < kScreenHeight) {
      if (!handleTouch(x, y)) {
        Serial.println("UI: no button at this point");
      }
      lastTouchAction = millis();
    } else {
      Serial.println("TOUCH: coordinates outside 800x480; check rotation mapping");
    }
  }
  previousTouch = isPressed;
}
}  // namespace valver
