#include <Arduino.h>
#include "config/SystemConfig.h"
#include "config/HardwareConfig.h"
#include "system/SystemState.h"

namespace {
constexpr const char* kBootMessage = "VALVER SMART DISPENSER";
constexpr const char* kFirmwareMessage = "Firmware boot in safe mode";
}

void printDiagnosticStatus();

void setup() {
  Serial.begin(valver::kDefaultBaudRate);
  delay(500);
  Serial.println();
  Serial.println("============================================");
  Serial.println(kBootMessage);
  Serial.printf("Firmware version: %s\n", valver::kFirmwareVersion);
  Serial.println(kFirmwareMessage);
  Serial.println("Motors disabled: waiting for validated configuration.");
  Serial.println("Status: BOOT -> INITIALIZING -> SAFE IDLE");
  Serial.println("============================================");
  printDiagnosticStatus();
}

void loop() {
  static uint32_t lastReport = 0;
  if (millis() - lastReport >= 5000UL) {
    printDiagnosticStatus();
    lastReport = millis();
  }
  delay(100);
}

void printDiagnosticStatus() {
  Serial.println("[DIAG] System initialized in safe mode.");
  Serial.printf("[DIAG] Project: %s\n", valver::kProjectName);
  Serial.printf("[DIAG] Firmware version: %s\n", valver::kFirmwareVersion);
  Serial.printf("[DIAG] Movement enabled: %s\n", valver::kMovementDisabled ? "NO" : "YES");
  Serial.printf("[DIAG] Motors available in hardware: %s\n", valver::kMotorsAvailable ? "YES" : "NO");
  Serial.printf("[DIAG] Simulation mode: %s\n", valver::kSimulationMode ? "ON" : "OFF");
  Serial.println("[DIAG] Actuator outputs remain inhibited.");
}
