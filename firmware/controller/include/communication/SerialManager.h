#pragma once

namespace valver {
class SerialManager {
public:
  SerialManager();
  void begin(unsigned long baudRate);
};
}
