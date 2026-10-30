#pragma once

#include <cstdint>

namespace valver {
struct PinConfig {
  std::uint8_t stepChannel1 = 0;
  std::uint8_t dirChannel1 = 0;
  std::uint8_t stepChannel2 = 0;
  std::uint8_t dirChannel2 = 0;
  std::uint8_t endstopA = 0;
  std::uint8_t endstopB = 0;
  std::uint8_t needleSensor = 0;
};
}
