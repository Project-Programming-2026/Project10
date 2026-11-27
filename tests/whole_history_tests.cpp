// 全履歴推定の単体テスト。実装のニュートン更新とは別の方法（二分法・最適性の条件）で
// 推定値を確かめ、試合順・記録の向き・評価時点・入力検証を確認する。

#include "project10/ratings/whole_history.hpp"

#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {

using project10::ratings::WholeHistoryEstimate;
using project10::ratings::WholeHistoryEstimator;

constexpr double scaleFactor = 173.7178;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Action>
void rejects(Action action) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("std::invalid_argument was not thrown");
}

double logistic(double x) {
  return 1.0 / (1.0 + std::exp(-x));
}

std::vector<WholeHistoryEstimate> fit(int numPlayers, double priorRD, const std::vector<Match>& matches) {
  WholeHistoryEstimator estimator(numPlayers, priorRD);
  estimator.addMatches(matches);
  return estimator.refit();
}

void testNoGames() {
  for (const auto& estimate : fit(3, 210.0, {})) {
    require(estimate.rating == 1500.0 && estimate.ratingDeviation == 210.0,
            "Without games, every player must keep the prior");
  }
}

void testTwoPlayers() {
  // Aが1回勝つ。対称性から、Glicko-2尺度でA = m、B = −mとなり、
  // mは 1 − σ(2m) = m / φ0² を満たす。二分法で独立に解いて照合する。
  constexpr double priorRD = 210.0;
  const double priorPrecision = std::pow(scaleFactor / priorRD, 2);
  double low = 0.0;
  double high = 10.0;
  for (int i = 0; i < 200; i++) {
    const double middle = 0.5 * (low + high);
    (1.0 - logistic(2.0 * middle) - middle * priorPrecision > 0.0 ? low : high) = middle;
  }
  const double m = 0.5 * (low + high);
  const double expected = logistic(2.0 * m);
  const double rd = scaleFactor / std::sqrt(priorPrecision + expected * (1.0 - expected));
  const auto estimates = fit(2, priorRD, {{0, 0, 0, 1, 1}});
  require(std::abs(estimates[0].rating - (1500.0 + scaleFactor * m)) < 1e-6 &&
          std::abs(estimates[1].rating - (1500.0 - scaleFactor * m)) < 1e-6,
          "Two-player ratings differ from the bisection solution");
  require(std::abs(estimates[0].ratingDeviation - rd) < 1e-6 &&
          std::abs(estimates[1].ratingDeviation - rd) < 1e-6,
          "Two-player RD differs from the Hessian formula");
}

// 事後最頻値では、各人の対数事後確率の勾配が0になる。RDもその点のヘッセ行列から計算し直す。
void testOptimality() {
  constexpr int numPlayers = 10;
  constexpr double priorRD = 300.0;
  const double priorPrecision = std::pow(scaleFactor / priorRD, 2);
  const auto abilities = generateLatentParameters(numPlayers, 0.0, 1.0, 21);
  const auto matches = generateMatches(abilities, 20, 1, 1.0, 22, 23);
  const auto estimates = fit(numPlayers, priorRD, matches);
  for (int player = 0; player < numPlayers; player++) {
    const double rating = (estimates[player].rating - 1500.0) / scaleFactor;
    double gradient = -rating * priorPrecision;
    double information = priorPrecision;
    for (const Match& match : matches) {
      if (match.playerA != player && match.playerB != player) continue;
      const bool isA = match.playerA == player;
      const double opponent = (estimates[isA ? match.playerB : match.playerA].rating - 1500.0) / scaleFactor;
      const double expected = logistic(rating - opponent);
      gradient += (isA ? match.resultA : 1 - match.resultA) - expected;
      information += expected * (1.0 - expected);
    }
    require(std::abs(gradient) < 1e-8, "Gradient must vanish at the posterior mode");
    require(std::abs(estimates[player].ratingDeviation - scaleFactor / std::sqrt(information)) < 1e-6,
            "RD differs from the Hessian at the posterior mode");
  }
}

// 全履歴の推定は、試合の順序とA・Bの記録の向きに依存しない。
void testOrderAndDirection() {
  constexpr int numPlayers = 8;
  const auto abilities = generateLatentParameters(numPlayers, 0.0, 1.0, 31);
  const auto matches = generateMatches(abilities, 15, 1, 1.0, 32, 33);
  auto reordered = matches;
  std::mt19937_64 generator(34);
  std::shuffle(reordered.begin(), reordered.end(), generator);
  for (std::size_t i = 0; i < reordered.size(); i += 2) {
    std::swap(reordered[i].playerA, reordered[i].playerB);
    reordered[i].resultA = 1 - reordered[i].resultA;
  }
  const auto original = fit(numPlayers, 250.0, matches);
  const auto other = fit(numPlayers, 250.0, reordered);
  for (int player = 0; player < numPlayers; player++) {
    require(std::abs(original[player].rating - other[player].rating) < 1e-7 &&
            std::abs(original[player].ratingDeviation - other[player].ratingDeviation) < 1e-7,
            "Estimates depend on match order or direction");
  }
}

void testPriorAndExtremes() {
  const auto abilities = generateLatentParameters(6, 0.0, 1.0, 41);
  const auto matches = generateMatches(abilities, 10, 1, 1.0, 42, 43);
  // 事前分布が狭いほど、推定値は1500へ縮む。
  const auto spread = [](const std::vector<WholeHistoryEstimate>& estimates) {
    double largest = 0.0;
    for (const auto& estimate : estimates) largest = std::max(largest, std::abs(estimate.rating - 1500.0));
    return largest;
  };
  require(spread(fit(6, 1.0, matches)) < 0.1, "A narrow prior must keep ratings near 1500");
  require(spread(fit(6, 100.0, matches)) < spread(fit(6, 400.0, matches)),
          "A wider prior must allow a wider spread");
  // 全勝しても、事前分布により有限の値になる。
  std::vector<Match> allWins;
  for (int round = 0; round < 50; round++) {
    for (int opponent = 1; opponent < 4; opponent++) allWins.push_back({0, 0, 0, opponent, 1});
  }
  const auto estimates = fit(4, 350.0, allWins);
  require(std::isfinite(estimates[0].rating) && estimates[0].rating > estimates[1].rating,
          "A player with only wins must get a finite, higher rating");
}

void testCheckpoints() {
  constexpr int numPlayers = 6;
  constexpr int interval = 5;
  const auto abilities = generateLatentParameters(numPlayers, 0.0, 1.0, 51);
  const auto matches = generateMatches(abilities, 20, 10, 1.0, 52, 53);
  std::map<std::tuple<int, int, int>, double> predictions;
  project10::ratings::predictWholeHistoryAtCheckpoints(numPlayers, interval, matches, 210.0,
    [&predictions](int matchesPerPlayer, int playerA, int playerB, double probability) {
      require(predictions.insert({{matchesPerPlayer, playerA, playerB}, probability}).second,
              "Duplicate prediction");
    });
  require(predictions.size() == 4 * numPlayers * (numPlayers - 1) / 2, "Missing predictions");
  // 10試合時点の予測は、最初の10ラウンドだけを新しく推定した結果と一致する（前回の解から始めても同じ）。
  const std::vector<Match> firstTen(matches.begin(), matches.begin() + 10 * numPlayers / 2);
  const auto estimates = fit(numPlayers, 210.0, firstTen);
  for (int a = 0; a < numPlayers; a++) {
    for (int b = a + 1; b < numPlayers; b++) {
      const double expected = project10::ratings::calculateWholeHistoryWinProbability(estimates[a], estimates[b]);
      require(std::abs(predictions.at({10, a, b}) - expected) < 1e-9, "Checkpoint differs from a fresh fit");
      require(std::abs(expected + project10::ratings::calculateWholeHistoryWinProbability(
                estimates[b], estimates[a]) - 1.0) < 1e-12, "Swapped probabilities must sum to 1");
    }
  }
}

void testInvalidInputs() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  rejects([] { WholeHistoryEstimator(0, 210.0); });
  for (const double invalid : {0.0, -1.0, nan, infinity}) {
    rejects([invalid] { WholeHistoryEstimator(2, invalid); });
  }
  // 不正な試合は拒否し、それまでの状態を変えない。
  WholeHistoryEstimator estimator(2, 210.0);
  estimator.addMatches({{0, 0, 0, 1, 1}});
  const auto before = estimator.refit();
  for (const Match& invalid : std::vector<Match>{
         {1, 0, -1, 1, 1}, {1, 0, 0, 2, 1}, {1, 0, 1, 1, 1}, {1, 0, 0, 1, 2}, {1, 0, 0, 1, -1}}) {
    rejects([&estimator, invalid] { estimator.addMatches({{1, 0, 1, 0, 1}, invalid}); });
  }
  // 再計算は前回の解から始まり、収束判定の幅（約1.7e-8）までは動く。拒否した組の1試合目
  // （BがAに勝つ）が追加されていれば両者のレートは等しくなり、差は100以上縮む。
  const auto after = estimator.refit();
  require(std::abs(after[0].rating - before[0].rating) < 1e-6 &&
          std::abs(after[1].rating - before[1].rating) < 1e-6,
          "Rejected matches must not be added");

  const auto ignore = [](int, int, int, double) {};
  const auto abilities = generateLatentParameters(4, 0.0, 1.0, 61);
  const auto matches = generateMatches(abilities, 4, 4, 1.0, 62, 63);
  using project10::ratings::predictWholeHistoryAtCheckpoints;
  rejects([&matches, ignore] { predictWholeHistoryAtCheckpoints(4, 0, matches, 210.0, ignore); });
  auto uneven = matches;
  uneven.push_back({99, 0, 0, 1, 1});  // 最後に0と1だけが1試合多い
  rejects([&uneven, ignore] { predictWholeHistoryAtCheckpoints(4, 2, uneven, 210.0, ignore); });
  auto selfMatch = matches;
  selfMatch.push_back({99, 0, 2, 2, 1});  // 最後の評価時点より後の自己対戦
  rejects([&selfMatch, ignore] { predictWholeHistoryAtCheckpoints(4, 4, selfMatch, 210.0, ignore); });
}

}  // namespace

int main() {
  try {
    testNoGames();
    testTwoPlayers();
    testOptimality();
    testOrderAndDirection();
    testPriorAndExtremes();
    testCheckpoints();
    testInvalidInputs();
    std::cout << "Whole-history unit tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
