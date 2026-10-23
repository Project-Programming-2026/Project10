// Glicko（初代）の単体テスト。原典の計算例との照合に加え、
// Step 1のRD増加・期間内の同時更新・入力検証を確認する。

#include "project10/ratings/glicko.hpp"
#include "project10/simulation/matches.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
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
  throw std::runtime_error("std::invalid_argument was not thrown");
}

bool sameState(const GlickoState& left, const GlickoState& right) {
  return left.rating == right.rating && left.rd == right.rd;
}

void testPaperExamples() {
  // 原典のStep 2の計算例：1500（RD 200）が1400（RD 30）に勝ち、1550（RD 100）と
  // 1700（RD 300）に負ける。Step 1後にちょうどこのRDになる値から始め、相手にも
  // Step 1が適用されることを確認する。2試合目は原典のプレイヤーをB側に置く。
  const std::vector<Match> matches = {{0, 0, 0, 1, 1}, {1, 0, 2, 0, 1}, {2, 0, 0, 3, 0}};
  for (const double rdGrowth : {0.0, 20.0}) {
    const auto oldRD = [rdGrowth](double rd) { return std::sqrt(rd * rd - rdGrowth * rdGrowth); };
    const std::vector<GlickoState> states = {
      {1500.0, oldRD(200.0)}, {1400.0, oldRD(30.0)}, {1550.0, oldRD(100.0)}, {1700.0, oldRD(300.0)}
    };
    const GlickoState next = updateGlickoPeriod(states, matches, 350.0, rdGrowth)[0];
    // 原典は途中の値を丸めて、r' = 1464、RD' = 151.4と報告している。
    require(std::abs(next.rating - 1464.0) < 0.5, "Rating differs from the paper example");
    require(std::abs(next.rd - 151.4) < 0.05, "RD differs from the paper example");
    // bc -l（scale = 30）で独立に計算した、丸めない値。
    require(std::abs(next.rating - 1464.106462756911) < 1e-9, "Rating differs from bc value");
    require(std::abs(next.rd - 151.398902447969) < 1e-9, "RD differs from bc value");
  }

  // 原典の予測例：1400（RD 80）が1500（RD 150）に勝つ確率は0.376。
  const double probability = calculateGlickoWinProbability({1400.0, 80.0}, {1500.0, 150.0});
  require(std::abs(probability - 0.376) < 0.0005, "Prediction differs from the paper example");
  require(std::abs(probability - 0.375987655713692) < 1e-12, "Prediction differs from bc value");
}

void testPredictionProperties() {
  constexpr double tolerance = 1e-12;
  require(calculateGlickoWinProbability({1500.0, 80.0}, {1500.0, 200.0}) == 0.5,
          "Equal ratings must give 0.5");
  for (const double ratingA : {1000.0, 1500.0, 2200.0}) {
    for (const double ratingB : {1000.0, 1600.0}) {
      const GlickoState stateA{ratingA, 60.0};
      const GlickoState stateB{ratingB, 250.0};
      const double probability = calculateGlickoWinProbability(stateA, stateB);
      require(std::isfinite(probability) && probability >= 0.0 && probability <= 1.0,
              "Probability must be finite and between 0 and 1");
      require(std::abs(probability + calculateGlickoWinProbability(stateB, stateA) - 1.0) <
              tolerance, "Swapped probabilities must sum to 1");
    }
  }
  // RDが大きいほど、同じレート差でも予測は0.5に近づく。
  require(calculateGlickoWinProbability({1600.0, 300.0}, {1500.0, 300.0}) <
          calculateGlickoWinProbability({1600.0, 30.0}, {1500.0, 30.0}),
          "Uncertain ratings must be closer to 0.5");
  const double largest = std::numeric_limits<double>::max();
  require(calculateGlickoWinProbability({largest, 30.0}, {-largest, 30.0}) == 1.0,
          "Extreme rating difference must give 1");
}

void testPeriodUpdate() {
  const auto initial = initializeGlickoStates(3, 1500.0, 350.0);
  require(initial.size() == 3, "Incorrect number of initial states");
  for (const GlickoState& state : initial) {
    require(sameState(state, {1500.0, 350.0}), "Incorrect initial state");
  }
  // 0が1に勝つ。2は対戦しない。
  const auto next = updateGlickoPeriod(initial, {{0, 0, 0, 1, 1}}, 350.0, 50.0);
  require(next[0].rating > 1500.0 && next[1].rating < 1500.0, "Winner must gain rating");
  require(std::abs((next[0].rating - 1500.0) - (1500.0 - next[1].rating)) < 1e-9,
          "Equal states must move symmetrically");
  require(next[0].rd == next[1].rd && next[0].rd < 350.0, "Games must decrease RD");
  require(sameState(next[2], {1500.0, 350.0}), "A player without games must not change");
  // 同じ結果をA・Bを入れ替えて記録しても、同じ状態になること。
  const auto swapped = updateGlickoPeriod(initial, {{0, 0, 1, 0, 0}}, 350.0, 50.0);
  for (std::size_t i = 0; i < next.size(); i++) {
    require(sameState(next[i], swapped[i]), "resultA was read in the wrong direction");
  }

  // 対戦のない期間のStep 1(b)：RD = min(sqrt(RD^2 + c^2), initialRD)。
  auto growing = next;
  for (int period = 1; period <= 30; period++) {
    const auto previous = growing;
    growing = updateGlickoPeriod(growing, {}, 350.0, 50.0);
    for (std::size_t i = 0; i < growing.size(); i++) {
      require(growing[i].rating == previous[i].rating, "Rating changed without games");
      require(growing[i].rd == std::min(std::hypot(previous[i].rd, 50.0), 350.0),
              "Incorrect RD growth between periods");
    }
  }
  require(growing[0].rd == 350.0, "RD must be capped at the unrated RD");
  require(updateGlickoPeriod(next, {}, 350.0, 0.0)[0].rd == next[0].rd,
          "rdGrowth = 0 must keep RD");

  // 原典p.6の例：RD 50の人が30期間対戦しないと350に戻るcは約63.2。
  // t期間後のRDを、閉じた式 min(sqrt(50^2 + c^2 * t), 350) と照合する。
  const double c = std::sqrt((350.0 * 350.0 - 50.0 * 50.0) / 30.0);
  require(std::abs(c - 63.2) < 0.05, "c differs from the paper example");
  std::vector<GlickoState> idle = {{1500.0, 50.0}};
  for (int t = 1; t <= 31; t++) {
    idle = updateGlickoPeriod(idle, {}, 350.0, c);
    const double expected = std::min(std::sqrt(50.0 * 50.0 + c * c * t), 350.0);
    require(std::abs(idle[0].rd - expected) < 1e-9, "RD differs from the paper's closed form");
  }
}

void testSimultaneousUpdate() {
  // 期間開始時の状態をばらしてから、同じ期間に0が1に、1が2に勝つ。
  const auto states = updateGlickoPeriod(
    initializeGlickoStates(3, 1500.0, 350.0), {{0, 0, 0, 1, 1}, {1, 0, 1, 2, 0}}, 350.0, 30.0
  );
  const std::vector<Match> period = {{2, 1, 0, 1, 1}, {3, 1, 1, 2, 1}};
  const auto next = updateGlickoPeriod(states, period, 350.0, 30.0);
  // 1は同じ期間に0とも対戦するが、2の更新には1の期間開始時の状態を使うこと。
  // 0についても同様。
  const auto onlyPlayer2 = updateGlickoPeriod(states, {period[1]}, 350.0, 30.0);
  const auto onlyPlayer0 = updateGlickoPeriod(states, {period[0]}, 350.0, 30.0);
  require(sameState(next[2], onlyPlayer2[2]), "Player 2 used a state updated in this period");
  require(sameState(next[0], onlyPlayer0[0]), "Player 0 used a state updated in this period");
}

void testGeneratedHistory() {
  // matches.csvと同じ形式の履歴：8人・各200試合・1期間10試合（第7回会議）。
  // 能力は生成器だけが使い、Glickoには渡さない。rdGrowth = 0はテスト用の値。
  const std::vector<double> abilities = {-3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0};
  const auto matches = generateMatches(abilities, 200, 10, 1.0, 1, 2);
  auto states = initializeGlickoStates(8, 1500.0, 350.0);
  std::vector<Match> period;
  int updatedPeriods = 0;
  for (const Match& match : matches) {
    if (!period.empty() && match.periodID != period.front().periodID) {
      states = updateGlickoPeriod(states, period, 350.0, 0.0);
      period.clear();
      updatedPeriods++;
    }
    period.push_back(match);
  }
  states = updateGlickoPeriod(states, period, 350.0, 0.0);  // 最後の期間。
  updatedPeriods++;
  require(updatedPeriods == 20, "200 matches per player must give 20 periods");
  // 7σの能力差が推定レートに反映されること。
  require(states[7].rating > states[0].rating, "Ratings must follow the strongest gap");
  require(calculateGlickoWinProbability(states[7], states[0]) > 0.9,
          "Strongest player must be predicted to beat the weakest");
}

void testExtremeInputs() {
  // Eがちょうど0になり、1/d^2も0になる。それでも更新結果は有限であること。
  const std::vector<GlickoState> states = {{1500.0, 350.0}, {1.0e6, 30.0}};
  for (const int resultA : {0, 1}) {
    const auto next = updateGlickoPeriod(states, {{0, 0, 0, 1, resultA}}, 350.0, 0.0);
    for (const GlickoState& state : next) {
      require(std::isfinite(state.rating) && std::isfinite(state.rd) && state.rd > 0.0,
              "Extreme ratings must give finite states");
    }
  }
}

void testInvalidInputs() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  rejects([] { initializeGlickoStates(0, 1500.0, 350.0); });
  rejects([] { initializeGlickoStates(-1, 1500.0, 350.0); });
  for (const double invalid : {0.0, -1.0, nan, infinity}) {
    rejects([invalid] { initializeGlickoStates(2, 1500.0, invalid); });
  }
  rejects([nan] { initializeGlickoStates(2, nan, 350.0); });

  const auto states = initializeGlickoStates(2, 1500.0, 350.0);
  rejects([] { updateGlickoPeriod({}, {}, 350.0, 50.0); });
  for (const double invalid : {0.0, -1.0, nan, infinity}) {
    rejects([&states, invalid] { updateGlickoPeriod(states, {}, invalid, 50.0); });
  }
  for (const double invalid : {-1.0, nan, infinity}) {
    rejects([&states, invalid] { updateGlickoPeriod(states, {}, 350.0, invalid); });
  }
  for (const GlickoState& invalid : {GlickoState{1500.0, 0.0}, GlickoState{nan, 350.0}}) {
    rejects([invalid] { updateGlickoPeriod({invalid, {1500.0, 350.0}}, {}, 350.0, 50.0); });
  }
  const std::vector<Match> invalidMatches = {
    {0, 0, -1, 1, 1},  // 未知のプレイヤーID
    {0, 0, 0, 2, 1},   // 未知のプレイヤーID
    {0, 0, 1, 1, 1},   // 自己対戦
    {0, 0, 0, 1, 2},   // 範囲外の勝敗
    {0, 0, 0, 1, -1},  // 範囲外の勝敗
    {0, -1, 0, 1, 1}   // 負の期間ID（period_idは0始まり）
  };
  for (const Match& invalid : invalidMatches) {
    rejects([&states, invalid] { updateGlickoPeriod(states, {invalid}, 350.0, 50.0); });
  }
  rejects([&states] {
    updateGlickoPeriod(states, {{0, 0, 0, 1, 1}, {1, 1, 0, 1, 0}}, 350.0, 50.0);
  });
  // 同じmatchIDの重複は拒否し、同じ相手との再戦（別のmatchID）は受け付ける。
  rejects([&states] {
    updateGlickoPeriod(states, {{0, 0, 0, 1, 1}, {0, 0, 0, 1, 1}}, 350.0, 50.0);
  });
  require(updateGlickoPeriod(states, {{0, 0, 0, 1, 1}, {1, 0, 1, 0, 1}}, 350.0, 50.0).size() == 2,
          "A rematch with a different matchID must be accepted");

  rejects([] { calculateGlickoWinProbability({1500.0, 0.0}, {1500.0, 350.0}); });
  rejects([infinity] { calculateGlickoWinProbability({1500.0, 350.0}, {infinity, 350.0}); });
}

void testWritePredictions() {
  // 生成器と同じ形式：4人・各4試合・1期間2試合。評価間隔2なので評価時点は2と4。
  const auto matches = generateMatches({0.0, 0.5, 1.0, 1.5}, 4, 2, 1.0, 3, 4);
  std::ostringstream output;
  writeGlickoPredictions(output, 7, 4, 2, matches, 1500.0, 350.0, 20.0);

  // 期待値：期間ごとにupdateGlickoPeriodを適用し、各期間の終了時の状態から予測する。
  std::vector<std::string> expected = {
    "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins"
  };
  auto states = initializeGlickoStates(4, 1500.0, 350.0);
  for (int periodID = 0; periodID < 2; periodID++) {
    std::vector<Match> period;
    for (const Match& match : matches) {
      if (match.periodID == periodID) {
        period.push_back(match);
      }
    }
    states = updateGlickoPeriod(states, period, 350.0, 20.0);
    for (int a = 0; a < 4; a++) {
      for (int b = a + 1; b < 4; b++) {
        std::ostringstream row;
        row << std::setprecision(std::numeric_limits<double>::max_digits10) << "7,"
            << (periodID + 1) * 2 << ',' << a << ',' << b << ','
            << calculateGlickoWinProbability(states[a], states[b]);
        expected.push_back(row.str());
      }
    }
  }
  std::istringstream lines(output.str());
  std::string line;
  std::size_t index = 0;
  while (std::getline(lines, line)) {
    require(index < expected.size() && line == expected[index], "Incorrect prediction CSV row");
    index++;
  }
  require(index == expected.size(), "Prediction CSV has missing rows");

  // 1期間1試合・評価間隔2では、評価時点2・4の期間の終了時だけ書き、1・3では書かない。
  const auto oneMatchPeriods = generateMatches({0.0, 0.5, 1.0, 1.5}, 4, 1, 1.0, 3, 4);
  std::ostringstream sparse;
  writeGlickoPredictions(sparse, 7, 4, 2, oneMatchPeriods, 1500.0, 350.0, 20.0);
  std::istringstream sparseLines(sparse.str());
  std::getline(sparseLines, line);  // ヘッダー
  std::size_t rows = 0;
  while (std::getline(sparseLines, line)) {
    const auto first = line.find(',');
    const auto point = line.substr(first + 1, line.find(',', first + 1) - first - 1);
    require(point == "2" || point == "4", "Predictions must be written only at evaluation points");
    rows++;
  }
  require(rows == 12, "Incorrect number of rows for one-match periods");

  std::ostringstream ignored;
  // 1期間2試合で評価間隔3だと、評価時点3が期間の途中に入る。
  rejects([&] { writeGlickoPredictions(ignored, 7, 4, 3, matches, 1500.0, 350.0, 20.0); });
  rejects([&] { writeGlickoPredictions(ignored, 7, 4, 0, matches, 1500.0, 350.0, 20.0); });
  rejects([&] { writeGlickoPredictions(ignored, -1, 4, 2, matches, 1500.0, 350.0, 20.0); });
  // 期間の終了時に各人の対戦数が揃わない履歴（0だけが2試合）。
  rejects([&] {
    writeGlickoPredictions(ignored, 7, 4, 2, {{0, 0, 0, 1, 1}, {1, 0, 0, 2, 0}},
                           1500.0, 350.0, 20.0);
  });
}

}  // namespace

int main() {
  try {
    testPaperExamples();
    testPredictionProperties();
    testPeriodUpdate();
    testSimultaneousUpdate();
    testGeneratedHistory();
    testExtremeInputs();
    testInvalidInputs();
    testWritePredictions();
    std::cout << "Glicko unit tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
