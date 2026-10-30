#pragma once

namespace valver {
class EndstopManager {
public:
  EndstopManager();
  bool isAnyEndstopActive() const;
};
}
