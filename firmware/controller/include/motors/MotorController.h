#pragma once

namespace valver {
class MotorController {
public:
  MotorController();
  bool isAvailable() const;
  void disableOutputs();
};
}
