#pragma once

namespace valver {
class TMC2209Manager {
public:
  TMC2209Manager();
  bool isDriverReady() const;
  void disableDrivers();
};
}
