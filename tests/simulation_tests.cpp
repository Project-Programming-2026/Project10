#include "project10/io/config.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/players.hpp"
#include "project10/simulation/probit.hpp"

#include <cmath>
#include <initializer_list>
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

void testWinProbabilities() {
  // 第7回会議の式による独立した計算例。Phi(1/sqrt(2))の既知値と照合する。
  constexpr double expected = 0.7602499389065233;
  constexpr double tolerance = 1e-12;
  require(calculateWinProbability(2.0, 2.0, 1.0) == 0.5, "Equal abilities must give 0.5");
  require(std::abs(calculateWinProbability(1.0, 0.0, 1.0) - expected) < tolerance,
          "Incorrect probit probability for ability difference 1");
  require(std::abs(calculateWinProbability(2.0, 0.0, 2.0) - expected) < tolerance,
          "Performance standard deviation was ignored");
  for (const double a : {-100.0, -1.0, 0.0, 1.0, 100.0}) {
    for (const double b : {-100.0, -1.0, 0.0, 1.0, 100.0}) {
      const double probability = calculateWinProbability(a, b, 1.0);
      require(std::isfinite(probability) && probability >= 0.0 && probability <= 1.0,
              "Probability must be finite and between 0 and 1");
      require(std::abs(probability + calculateWinProbability(b, a, 1.0) - 1.0) < tolerance,
              "Swapped probabilities must sum to 1");
    }
  }
  // 有限な入力でも、能力差・分母のオーバーフローや微小値の丸めに注意する。
  const double largest = std::numeric_limits<double>::max();
  const double smallest = std::numeric_limits<double>::denorm_min();
  require(std::abs(calculateWinProbability(largest, -largest, largest) -
                  0.9213503964748574) < tolerance, "Overflow in finite probit inputs");
  require(std::abs(calculateWinProbability(smallest, 0.0, smallest) - expected) < tolerance,
          "Underflow in small probit inputs");
  rejects([] { calculateWinProbability(0.0, 0.0, 0.0); });
  rejects([] { calculateWinProbability(0.0, 0.0, -1.0); });
  const double nonFiniteValues[] = {
    std::numeric_limits<double>::quiet_NaN(),
    std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity()
  };
  for (const double invalid : nonFiniteValues) {
    rejects([invalid] { calculateWinProbability(invalid, 0.0, 1.0); });
    rejects([invalid] { calculateWinProbability(0.0, invalid, 1.0); });
    rejects([invalid] { calculateWinProbability(0.0, 0.0, invalid); });
  }
}

}  // namespace

int main() {
  try {
    testWinProbabilities();
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
