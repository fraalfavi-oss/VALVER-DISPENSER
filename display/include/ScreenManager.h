#pragma once

namespace valver {
class ScreenManager {
public:
  ScreenManager();
  bool initialize();
  void update();

private:
  bool initialized_ = false;
};
}
