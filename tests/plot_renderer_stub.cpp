// CLIの失敗処理を検証するための代替プログラム。実際の描画はplot_renderでgnuplotを使って確認する。
#include <cstdlib>
#include <fstream>
#include <string>

int main() {
  const auto* value = std::getenv("PROJECT10_TEST_RENDER");
  const std::string mode = value ? value : "success";
  if (mode == "fail") return 42;
  if (mode == "empty") return 0;
  std::ifstream script("plot.gp");
  std::string line;
  const std::string prefix = "set output '";
  bool wrote = false;
  while (std::getline(script, line)) {
    const auto end = line.compare(0, prefix.size(), prefix) == 0 ? line.find('\'', prefix.size()) : std::string::npos;
    if (end == std::string::npos) continue;
    const auto file = line.substr(prefix.size(), end - prefix.size());
    const bool png = file.size() >= 4 && file.substr(file.size() - 4) == ".png";
    std::ofstream(file, std::ios::binary) << (png ? "\x89PNG\r\n\x1a\nfixture" : "<svg></svg>");
    wrote = true;
  }
  if (!wrote) std::ofstream("mse.svg") << "<svg></svg>";
  return 0;
}
