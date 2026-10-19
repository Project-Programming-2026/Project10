#include "project10/io/config.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/players.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Action>
void rejects(Action action) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("std::invalid_argumentが送出されませんでした");
}

}  // namespace

int main() {
  try {
    const auto players = generatePlayers(8);
    require(players.size() == 8, "プレイヤーIDの件数が正しくありません");
    for (int id = 0; id < 8; id++) {
      require(players[id] == id, "プレイヤーIDが不正か重複しています");
    }
    rejects([] { generatePlayers(0); });
    rejects([] { generatePlayers(-1); });
    const auto first = generateLatentParameters(8, 0.0, 1.0, 42);
    const auto repeated = generateLatentParameters(8, 0.0, 1.0, 42);
    require(first.size() == 8, "潜在能力の件数が正しくありません");
    require(first == repeated, "同じシードで潜在能力を再現できませんでした");
    require(first != generateLatentParameters(8, 0.0, 1.0, 43), "シードを変更しても潜在能力が変わりませんでした");
    for (const double value : first) {
      require(std::isfinite(value), "潜在能力に非有限値が含まれています");
    }
    rejects([] { generateLatentParameters(0, 0.0, 1.0, 42); });
    rejects([] { generateLatentParameters(8, 0.0, 0.0, 42); });
    rejects([] { generateLatentParameters(8, 0.0, -1.0, 42); });
    rejects([] {
      generateLatentParameters(8, std::numeric_limits<double>::quiet_NaN(), 1.0, 42);
    });
    rejects([] {
      generateLatentParameters(8, 0.0, std::numeric_limits<double>::infinity(), 42);
    });
    require(project10::io::runDirectoryName(0) == "run_0000", "run IDを4桁へゼロ埋めできませんでした");
    require(project10::io::runDirectoryName(12345) == "run_12345", "run IDが途中で切り捨てられました");
    rejects([] { project10::io::runDirectoryName(-1); });
    std::cout << "生成器の単体確認に成功しました\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
