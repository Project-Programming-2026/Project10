// 調整実験用Glicko-2の単体テスト。原典の計算例との照合、既定値での既存実装との一致、
// 期間更新の性質、入力検証を確認する。引数は作業ディレクトリ（既存実装の入出力に使う）。

#include "project10/ratings/glicko2_tunable.hpp"

#include "project10/io/csv.hpp"
#include "project10/ratings/glicko2.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/players.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using project10::ratings::Glicko2Parameters;
using project10::ratings::Glicko2State;

// 原典Step 1(a)の値。既存のglicko2.cppもこの値を使う。
const Glicko2Parameters paperDefaults{350.0, 0.06, 0.5};

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

bool sameState(const Glicko2State& left, const Glicko2State& right) {
  return left.rating == right.rating && left.ratingDeviation == right.ratingDeviation &&
    left.volatility == right.volatility;
}

void testPaperExample() {
  // 原典の計算例：1500（RD 200、σ 0.06）が1400（RD 30）に勝ち、1550（RD 100）と
  // 1700（RD 300）に負ける。τ = 0.5。2試合目は原典のプレイヤーをB側に置く。
  const std::vector<Glicko2State> states = {
    {1500.0, 200.0, 0.06}, {1400.0, 30.0, 0.06}, {1550.0, 100.0, 0.06}, {1700.0, 300.0, 0.06}
  };
  const std::vector<Match> matches = {{0, 0, 0, 1, 1}, {1, 0, 2, 0, 1}, {2, 0, 0, 3, 0}};
  const Glicko2State next = project10::ratings::updateGlicko2Period(states, matches, 0.5)[0];
  // 原典は途中の値を4桁に丸めて、r' = 1464.06、RD' = 151.52、σ' = 0.05999と報告している。
  // μ'・φ'の4桁目の丸めは、レート尺度で約±0.0087に当たる。
  require(std::abs(next.rating - 1464.06) < 0.05, "Rating differs from the paper example");
  require(std::abs(next.ratingDeviation - 151.52) < 0.01, "RD differs from the paper example");
  require(std::abs(next.volatility - 0.05999) < 0.00001, "Volatility differs from the paper example");
}

// 既存のglicko2.cppは定数が固定で、1プロセスで1回だけ呼べる。既定値の予測と最終レートが
// 一致することを確認する。既存実装はCSVへ有効数字6桁で書くため、次の差を許容する。
// - 予測勝率：6桁の丸め（5e-7以下）と、予測式の定数q（既存0.0057565、本実装ln(10)/400）の
//   差。2026-11-25の確認では最大8.4e-7だったため、2e-6とする。
// - 最終レート：1500前後の値の6桁の丸め（0.005以下）。
void testMatchesExistingImplementation(const std::filesystem::path& workDir) {
  constexpr int numPlayers = 12;
  constexpr int matchesPerPlayer = 30;
  constexpr int period = 10;  // 既存実装は期間・評価間隔10に固定
  const auto abilities = generateLatentParameters(numPlayers, 0.0, 1.0, 11);
  const auto matches = generateMatches(abilities, matchesPerPlayer, period, 1.0, 12, 13);
  std::filesystem::create_directories(workDir);
  writePlayers(generatePlayers(numPlayers), (workDir / "players.csv").string());
  writeMatches(matches, (workDir / "matches.csv").string());
  generate_glicko2_prediction(7, workDir);

  std::map<std::tuple<int, int, int>, double> expected;
  project10::ratings::predictGlicko2AtCheckpoints(numPlayers, period, matches, paperDefaults,
    [&expected](int matchesPerPlayer, int playerA, int playerB, double probability) {
      expected[{matchesPerPlayer, playerA, playerB}] = probability;
    });
  std::ifstream predictions(workDir / "glicko2.csv");
  std::string line;
  require(std::getline(predictions, line) &&
          line == "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins",
          "Unexpected header of the existing Glicko-2 predictions");
  std::size_t rows = 0;
  while (std::getline(predictions, line)) {
    std::istringstream row(line);
    int runID;
    int matchesPerPlayer;
    int playerA;
    int playerB;
    double probability;
    char comma;
    row >> runID >> comma >> matchesPerPlayer >> comma >> playerA >> comma >> playerB >> comma
        >> probability;
    const auto found = expected.find({matchesPerPlayer, playerA, playerB});
    require(row && runID == 7 && found != expected.end(), "Unexpected existing prediction row");
    require(std::abs(found->second - probability) < 2e-6, "Prediction differs from glicko2.cpp");
    rows++;
  }
  require(rows == expected.size(), "Prediction count differs from glicko2.cpp");

  auto states = project10::ratings::initializeGlicko2States(numPlayers, paperDefaults);
  for (int periodID = 0; periodID < matchesPerPlayer / period; periodID++) {
    std::vector<Match> periodMatches;
    for (const Match& match : matches) {
      if (match.periodID == periodID) periodMatches.push_back(match);
    }
    states = project10::ratings::updateGlicko2Period(states, periodMatches, paperDefaults.tau);
  }
  std::ifstream ratings(workDir / "glicko2_rating.csv");
  require(std::getline(ratings, line) && line == "rating", "Unexpected header of glicko2_rating.csv");
  for (const Glicko2State& state : states) {
    require(std::getline(ratings, line) && std::abs(std::stod(line) - state.rating) < 0.006,
            "Final rating differs from glicko2.cpp");
  }
}

void testPeriodUpdate() {
  const auto initial = project10::ratings::initializeGlicko2States(3, paperDefaults);
  for (const Glicko2State& state : initial) {
    require(sameState(state, {1500.0, 350.0, 0.06}), "Incorrect initial state");
  }
  // 0が1に勝つ。2は対戦しない。
  const auto next = project10::ratings::updateGlicko2Period(initial, {{0, 0, 0, 1, 1}}, 0.5);
  require(next[0].rating > 1500.0 && next[1].rating < 1500.0, "Winner must gain rating");
  require(std::abs((next[0].rating - 1500.0) - (1500.0 - next[1].rating)) < 1e-9,
          "Equal states must move symmetrically");
  require(next[0].ratingDeviation < 350.0, "Games must decrease RD");
  // 対戦しなかった人はStep 6だけ：レートとσは同じで、φ' = sqrt(φ^2 + σ^2)。
  require(next[2].rating == 1500.0 && next[2].volatility == 0.06, "Absent player changed");
  require(std::abs(next[2].ratingDeviation - 173.7178 * std::hypot(350.0 / 173.7178, 0.06)) < 1e-9,
          "Absent player's RD must follow Step 6");
  // 同じ結果をA・Bを入れ替えて記録しても、同じ状態になること。
  const auto swapped = project10::ratings::updateGlicko2Period(initial, {{0, 0, 1, 0, 0}}, 0.5);
  for (std::size_t i = 0; i < next.size(); i++) {
    require(sameState(next[i], swapped[i]), "resultA was read in the wrong direction");
  }
  const double probability = project10::ratings::calculateGlicko2WinProbability(next[0], next[1]);
  require(probability > 0.5 &&
          std::abs(probability + project10::ratings::calculateGlicko2WinProbability(next[1], next[0]) - 1.0) < 1e-12,
          "Swapped probabilities must sum to 1");
}

void testExtremeInputs() {
  // Eがちょうど0になり、vの逆数が0になる。正常値に置き換えずに数値エラーとして止めること。
  const std::vector<Glicko2State> states = {{1500.0, 350.0, 0.06}, {1.0e6, 30.0, 0.06}};
  try {
    project10::ratings::updateGlicko2Period(states, {{0, 0, 0, 1, 0}}, 0.5);
  } catch (const std::runtime_error&) {
    return;
  }
  throw std::runtime_error("A non-finite variance was not reported");
}

void testInvalidInputs() {
  using project10::ratings::initializeGlicko2States;
  using project10::ratings::predictGlicko2AtCheckpoints;
  using project10::ratings::updateGlicko2Period;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  rejects([] { initializeGlicko2States(0, paperDefaults); });
  for (const double invalid : {0.0, -1.0, nan, infinity}) {
    rejects([invalid] { initializeGlicko2States(2, {invalid, 0.06, 0.5}); });
    rejects([invalid] { initializeGlicko2States(2, {350.0, invalid, 0.5}); });
    rejects([invalid] { initializeGlicko2States(2, {350.0, 0.06, invalid}); });
  }

  const auto states = initializeGlicko2States(2, paperDefaults);
  rejects([] { updateGlicko2Period({}, {}, 0.5); });
  rejects([&states] { updateGlicko2Period(states, {}, 0.0); });
  rejects([] { updateGlicko2Period({{1500.0, 350.0, 0.0}, {1500.0, 350.0, 0.06}}, {}, 0.5); });
  const std::vector<Match> invalidMatches = {
    {0, 0, -1, 1, 1},  // 未知のプレイヤーID
    {0, 0, 0, 2, 1},   // 未知のプレイヤーID
    {0, 0, 1, 1, 1},   // 自己対戦
    {0, 0, 0, 1, 2},   // 範囲外の勝敗
    {0, 0, 0, 1, -1}   // 範囲外の勝敗
  };
  for (const Match& invalid : invalidMatches) {
    rejects([&states, invalid] { updateGlicko2Period(states, {invalid}, 0.5); });
  }
  rejects([&states] { updateGlicko2Period(states, {{0, 0, 0, 1, 1}, {1, 1, 0, 1, 0}}, 0.5); });

  // 評価間隔が不正な場合と、評価時点（2試合）が期間（4試合）の途中にある場合。
  const auto abilities = generateLatentParameters(4, 0.0, 1.0, 1);
  const auto matches = generateMatches(abilities, 4, 4, 1.0, 2, 3);
  const auto ignore = [](int, int, int, double) {};
  rejects([&matches, ignore] { predictGlicko2AtCheckpoints(4, 0, matches, paperDefaults, ignore); });
  rejects([&matches, ignore] { predictGlicko2AtCheckpoints(4, 2, matches, paperDefaults, ignore); });
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::invalid_argument("usage: glicko2_tunable_tests WORK_DIRECTORY");
    testPaperExample();
    testMatchesExistingImplementation(argv[1]);
    testPeriodUpdate();
    testExtremeInputs();
    testInvalidInputs();
    std::cout << "Tunable Glicko-2 unit tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
