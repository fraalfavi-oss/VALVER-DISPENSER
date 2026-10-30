#pragma once

namespace valver {
class SafetyManager {
public:
  SafetyManager();
  bool isFaultPresent() const;
  void acknowledgeFault();
};
}
