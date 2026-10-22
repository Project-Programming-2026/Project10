// CLIの失敗処理専用。実描画の確認にはplot_renderで本物のgnuplotを使う。
#include <cstdlib>
#include <fstream>
#include <string>

int main() {
  const auto* value = std::getenv("PROJECT10_TEST_RENDER");
  const std::string mode = value ? value : "success";
  if (mode == "fail") return 42;
  if (mode == "empty") return 0;
  std::ofstream("mse.svg") << "<svg></svg>";
  return 0;
}
