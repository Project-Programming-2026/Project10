#include "project10/io/config.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/players.hpp"
#include "project10/simulation/probit.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

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

void checkMatches(const std::vector<Match>& matches, int players, int rounds, int period) {
  const auto perRound = static_cast<std::size_t>(players / 2);
  require(matches.size() == perRound * rounds, "Incorrect match count");
  std::vector<int> counts(players, 0);
  std::vector<bool> seen(players, false);
  for (std::size_t index = 0; index < matches.size(); ++index) {
    const auto& match = matches[index];
    if (index % perRound == 0) {
      std::fill(seen.begin(), seen.end(), false);
    }
    require(match.matchID == index, "Non-consecutive match ID");
    require(match.periodID == static_cast<int>(index / perRound) / period,
            "Incorrect period or boundary");
    require(match.playerA >= 0 && match.playerA < players &&
            match.playerB >= 0 && match.playerB < players, "Unknown player");
    require(match.playerA != match.playerB, "Self match");
    require(!seen[match.playerA] && !seen[match.playerB], "Duplicate player in round");
    seen[match.playerA] = seen[match.playerB] = true;
    ++counts[match.playerA];
    ++counts[match.playerB];
    require(match.resultA == 0 || match.resultA == 1, "Invalid result");
  }
  for (const int count : counts) {
    require(count == rounds, "Incorrect matches per player");
  }
}

void testMatches() {
  const std::vector<double> abilities(8, 0.0);
  const auto first = generateMatches(abilities, 5, 2, 1.0, 42, 43);
  const auto repeated = generateMatches(abilities, 5, 2, 1.0, 42, 43);
  const auto resultChanged = generateMatches(abilities, 5, 2, 1.0, 42, 44);
  const auto matchingChanged = generateMatches(abilities, 5, 2, 1.0, 44, 43);
  const auto modelChanged = generateMatches({-3, -2, -1, 0, 1, 2, 3, 4}, 5, 2, 2.0, 42, 43);
  checkMatches(first, 8, 5, 2);  // 2+2+1ラウンドの期間、最後の端数も検証。
  bool differentResult = false;
  bool differentMatching = false;
  for (std::size_t i = 0; i < first.size(); ++i) {
    require(first[i].matchID == repeated[i].matchID &&
            first[i].periodID == repeated[i].periodID &&
            first[i].playerA == repeated[i].playerA &&
            first[i].playerB == repeated[i].playerB &&
            first[i].resultA == repeated[i].resultA, "Matches were not reproduced");
    require(first[i].playerA == resultChanged[i].playerA &&
            first[i].playerB == resultChanged[i].playerB &&
            first[i].playerA == modelChanged[i].playerA &&
            first[i].playerB == modelChanged[i].playerB,
            "Results or abilities changed matching RNG");
    require(first[i].resultA == matchingChanged[i].resultA,
            "Matching RNG changed result draws for equal abilities");
    differentResult |= first[i].resultA != resultChanged[i].resultA;
    differentMatching |= first[i].playerA != matchingChanged[i].playerA ||
      first[i].playerB != matchingChanged[i].playerB;
  }
  require(differentResult, "Result seed was ignored");
  require(differentMatching, "Matching seed was ignored");
  checkMatches(generateMatches(abilities, 1, 10, 1.0, 0, 0), 8, 1, 10);
  checkMatches(generateMatches(abilities, 4, 2, 1.0, 0, 0), 8, 4, 2);
  // 2人なら必ず再戦。ほぼ確実な勝敗でresult_aの向きと既存の式への接続を確認。
  const double largest = std::numeric_limits<double>::max();
  const auto certain = generateMatches({largest, -largest}, 6, 1, 1.0, 0, 0);
  checkMatches(certain, 2, 6, 1);
  for (const auto& match : certain) {
    require(match.resultA == (match.playerA == 0 ? 1 : 0), "Wrong winning side");
  }
  rejects([] { generateMatches({}, 2, 1, 1.0, 0, 0); });
  rejects([] { generateMatches({0}, 2, 1, 1.0, 0, 0); });
  rejects([] { generateMatches({0, 0, 0}, 2, 1, 1.0, 0, 0); });
  for (const int invalid : {0, -1}) {
    rejects([invalid] { generateMatches({0, 0}, invalid, 1, 1.0, 0, 0); });
    rejects([invalid] { generateMatches({0, 0}, 2, invalid, 1.0, 0, 0); });
  }
  for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
    rejects([invalid] { generateMatches({0, 0}, 2, 1, invalid, 0, 0); });
  }
  for (const double invalid : {std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
    rejects([invalid] { generateMatches({0, invalid}, 2, 1, 1.0, 0, 0); });
  }
  // 依頼された初期条件。全100,000試合の各人200試合・全20期間を走査する。
  const auto initial = generateMatches(
    generateLatentParameters(1000, 0.0, 1.0, 2026101601), 200, 10, 1.0,
    2026101602, 2026101603
  );
  checkMatches(initial, 1000, 200, 10);
  require(initial.back().periodID == 19, "Initial experiment must have 20 periods");
}

}  // namespace

int main() {
  try {
    testWinProbabilities();
    testMatches();
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
