#pragma once

namespace valver {
class FaultManager {
public:
  FaultManager();
  void reportFault(const char* message);
};
}
