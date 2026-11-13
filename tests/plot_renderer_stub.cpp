// CLIの失敗処理を検証するための代替プログラム。実際の描画はplot_renderでgnuplotを使って確認する。
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
