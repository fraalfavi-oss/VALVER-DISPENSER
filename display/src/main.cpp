#include <Arduino.h>

#include "ScreenManager.h"

namespace {
valver::ScreenManager screenManager;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("VALVER SMART DISPENSER - HMI");
  Serial.println("Booting CrowPanel 7-inch UI in safe mode.");

  if (!screenManager.initialize()) {
    Serial.println("HMI initialization failed; outputs remain unused.");
  }
}

void loop() {
  screenManager.update();
  delay(5);
}