#pragma once

#include "system/SystemState.h"

namespace valver {
class SystemManager {
public:
  SystemManager();
  void initialize();
  SystemState getState() const;
  void setState(SystemState newState);
  bool isSafeToMove() const;
private:
  SystemState currentState_;
};
}
