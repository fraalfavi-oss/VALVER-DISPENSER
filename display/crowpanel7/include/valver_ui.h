#pragma once
#include <stdint.h>
// VALVER SMART DISPENSER 7-inch CrowPanel LVGL 9.1 offline interface demo.
// This phase intentionally has NO motor UART, no GPIO outputs, no actuations.
void valver_ui_init();
void valver_ui_tick();

// On-screen GT911 identification and touch test diagnostics.
void valver_ui_set_touch_status(uint8_t i2c_address);
void valver_ui_note_touch(int x,int y);
