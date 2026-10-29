// ranked_glicko（Glicko初代版）の単体テスト。提案ノートの分布倍率の数値例との照合に加え、
// レート帯の境界、更新前の所属帯の使用、α=0で通常のGlickoと一致すること、入力検証を確認する。

#include "project10/ratings/glicko.hpp"
#include "project10/ratings/ranked_glicko.hpp"
#include "project10/simulation/matches.hpp"

#include <cmath>
#include <iostream>
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

// 帯6（1450以上1550未満）だけがreference6、残りを他の12帯へ均等に配った基準の割合。
std::vector<double> referenceWithCenter(double reference6) {
  std::vector<double> ratios(rankedGlickoBandCount, (1.0 - reference6) / 12.0);
  ratios[6] = reference6;
  return ratios;
}

// 提案ノートの数値例と同じ感度0.05にした、テスト用の設定。
RankedGlickoParameters parametersWith(double alpha) {
  return {1500.0, 350.0, 20.0, alpha, 0.05};
}

void testBands() {
  // Discordでの合意：950未満、950〜2050の100刻み、2050以上の13帯。帯6が1450〜1550。
  require(rankedGlickoBand(-1.0e6) == 0 && rankedGlickoBand(949.99) == 0, "Lowest band");
  require(rankedGlickoBand(950.0) == 1 && rankedGlickoBand(1049.99) == 1, "Band 1");
  require(rankedGlickoBand(1449.99) == 5 && rankedGlickoBand(1450.0) == 6, "Band 5/6 edge");
  require(rankedGlickoBand(1500.0) == 6 && rankedGlickoBand(1549.99) == 6, "Center band");
  require(rankedGlickoBand(1550.0) == 7 && rankedGlickoBand(2049.99) == 11, "Band 7/11");
  require(rankedGlickoBand(2050.0) == 12 && rankedGlickoBand(1.0e6) == 12, "Highest band");
  rejects([] { rankedGlickoBand(std::nan("")); });
}

void testBandRatios() {
  const auto ratios = calculateBandRatios(
    {{900.0, 50.0}, {1000.0, 50.0}, {1500.0, 50.0}, {1520.0, 50.0}, {2100.0, 50.0}}
  );
  require(ratios.size() == 13, "There must be 13 bands");
  require(ratios[0] == 0.2 && ratios[1] == 0.2 && ratios[6] == 0.4 && ratios[12] == 0.2,
          "Incorrect band ratios");
  rejects([] { calculateBandRatios({}); });
}

void testDistributionMultiplier() {
  constexpr double tolerance = 1e-12;
  // 提案ノート5.3節：α=0.25、c=0.05で、割合の差0・5・10ポイントはD=1.000・1.125・1.200。
  require(std::abs(calculateDistributionMultiplier(0.20, 0.20, 0.25, 0.05) - 1.000) < tolerance,
          "D must be 1 without error");
  require(std::abs(calculateDistributionMultiplier(0.25, 0.20, 0.25, 0.05) - 1.125) < tolerance,
          "D differs from the 5-point example");
  require(std::abs(calculateDistributionMultiplier(0.30, 0.20, 0.25, 0.05) - 1.200) < tolerance,
          "D differs from the 10-point example (also section 7.3)");
  require(std::abs(calculateDistributionMultiplier(0.10, 0.20, 0.25, 0.05) - 1.200) < tolerance,
          "Excess and shortage must be treated alike");
  // ずれが最大でも1 + αを超えない。α=0なら常に1。
  require(calculateDistributionMultiplier(1.0, 0.0, 0.25, 0.05) < 1.25, "D must stay below 1 + alpha");
  require(calculateDistributionMultiplier(1.0, 0.0, 0.0, 0.05) == 1.0, "alpha = 0 must give 1");
  rejects([] { calculateDistributionMultiplier(0.2, 0.2, -0.1, 0.05); });
  rejects([] { calculateDistributionMultiplier(0.2, 0.2, 0.25, 0.0); });
  rejects([] { calculateDistributionMultiplier(1.2, 0.2, 0.25, 0.05); });
  // 定数を省略したときは、ranked_glicko2（PR #25）と同じα=0.25、c=0.01になる。
  const RankedGlickoParameters defaults{1500.0, 350.0, 20.0};
  require(defaults.alpha == 0.25 && defaults.sensitivity == 0.01, "Defaults must match PR #25");
}

void testRankedUpdate() {
  // 全員1500（帯6の割合1.0）、基準の帯6は0.2。0が1に勝ち、2・3は対戦しない。
  const auto states = initializeGlickoStates(4, 1500.0, 350.0);
  const std::vector<Match> period = {{0, 0, 0, 1, 1}};
  const auto glicko = updateGlickoPeriod(states, period, 350.0, 20.0);
  const auto ranked = updateRankedGlickoPeriod(
    states, period, referenceWithCenter(0.2), parametersWith(0.25)
  );
  const double expectedD = 1.0 + 0.25 * 0.64 / (0.64 + 0.05 * 0.05);  // e = (1.0 - 0.2)²
  require(std::abs((ranked[0].rating - 1500.0) - expectedD * (glicko[0].rating - 1500.0)) < 1e-9,
          "Rating change must be D times the Glicko change");
  require(std::abs((ranked[1].rating - 1500.0) - expectedD * (glicko[1].rating - 1500.0)) < 1e-9,
          "Loser's change must also be scaled");
  require(ranked[0].rd == glicko[0].rd && ranked[1].rd == glicko[1].rd,
          "RD must be the Glicko value");
  require(ranked[2].rating == 1500.0 && ranked[2].rd == glicko[2].rd,
          "A player without games must keep the rating");

  // 基準が現在の割合と同じなら、D=1で通常のGlickoと同じになる。
  const auto same = updateRankedGlickoPeriod(
    states, period, calculateBandRatios(states), parametersWith(0.25)
  );
  require(std::abs(same[0].rating - glicko[0].rating) < 1e-9, "D = 1 must match Glicko");
}

void testPreUpdateBand() {
  // 1549（帯6）が1300（帯4）に勝ち、更新後は帯7へ移る。倍率は更新前の帯6のものを使う。
  const std::vector<GlickoState> states = {{1549.0, 350.0}, {1300.0, 350.0}};
  const std::vector<Match> period = {{0, 0, 0, 1, 1}};
  std::vector<double> reference(rankedGlickoBandCount, 0.0);
  reference[4] = 0.1;
  reference[6] = 0.1;
  reference[7] = 0.8;
  const auto glicko = updateGlickoPeriod(states, period, 350.0, 20.0);
  require(rankedGlickoBand(glicko[0].rating) == 7, "The winner must move to band 7");
  const auto ranked = updateRankedGlickoPeriod(states, period, reference, parametersWith(0.25));
  const double band6D = 1.0 + 0.25 * 0.16 / (0.16 + 0.05 * 0.05);  // P = 0.5、Q = 0.1
  require(std::abs((ranked[0].rating - 1549.0) - band6D * (glicko[0].rating - 1549.0)) < 1e-9,
          "The multiplier of the pre-update band must be used");
}

void testInvalidParameters() {
  const auto states = initializeGlickoStates(2, 1500.0, 350.0);
  const std::vector<Match> period = {{0, 0, 0, 1, 1}};
  const auto parameters = parametersWith(0.25);
  rejects([&] { updateRankedGlickoPeriod(states, period, {0.5, 0.5}, parameters); });
  rejects([&] { updateRankedGlickoPeriod(states, period, referenceWithCenter(1.2), parameters); });
  auto shortTotal = referenceWithCenter(0.2);
  shortTotal[0] -= 0.01;
  rejects([&] { updateRankedGlickoPeriod(states, period, shortTotal, parameters); });
  const auto center = referenceWithCenter(0.2);
  rejects([&] { updateRankedGlickoPeriod(states, period, center, parametersWith(-0.1)); });
  auto noSensitivity = parameters;
  noSensitivity.sensitivity = 0.0;
  rejects([&] { updateRankedGlickoPeriod(states, period, center, noSensitivity); });
}

std::vector<double> readRatios(const std::string& text) {
  std::istringstream input(text);
  return readReferenceRatios(input);
}

void testReadReferenceRatios() {
  // glicko_rating.csvの形式。testBandRatiosと同じ5人なので、同じ割合になる。
  const auto ratios = readRatios("rating\n900\n1000\n1500\n1520.25\n2100\n");
  require(ratios == calculateBandRatios(
            {{900.0, 50.0}, {1000.0, 50.0}, {1500.0, 50.0}, {1520.25, 50.0}, {2100.0, 50.0}}),
          "Incorrect reference ratios");
  // Windowsで作られた基準ファイル（CRLF）も同じに読む。
  require(readRatios("rating\r\n900\r\n1000\r\n1500\r\n1520.25\r\n2100\r\n") == ratios,
          "CRLF must be accepted");
  // 見出し違い・空・人数0・数値以外・列の追加・空行・非有限値は拒否する。
  for (const char* invalid : {"rate\n1500\n", "", "rating\n", "rating\nabc\n", "rating\n1500,1\n",
                              "rating\n1500\n\n1600\n", "rating\nnan\n", "rating\ninf\n"}) {
    rejects([invalid] { readRatios(invalid); });
  }
}

void testWritePredictions() {
  const auto matches = generateMatches({0.0, 0.5, 1.0, 1.5}, 4, 2, 1.0, 3, 4);
  const auto reference = referenceWithCenter(0.2);
  std::ostringstream glicko;
  writeGlickoPredictions(glicko, 7, 4, 2, matches, 1500.0, 350.0, 20.0);
  // α=0なら分布倍率は常に1なので、通常のGlickoの予測CSVと完全に一致する。
  std::ostringstream withoutBoost;
  writeRankedGlickoPredictions(withoutBoost, 7, 4, 2, matches, reference, parametersWith(0.0));
  require(withoutBoost.str() == glicko.str(), "alpha = 0 must reproduce Glicko predictions");

  // 渡した基準Qを、各期間の更新にそのまま使う（同じrunの履歴からQを作らない）。
  std::ostringstream expected;
  writeGlickoPredictionsWith(
    expected, 7, 2, matches, initializeGlickoStates(4, 1500.0, 350.0),
    [&reference](const std::vector<GlickoState>& states, const std::vector<Match>& period) {
      return updateRankedGlickoPeriod(states, period, reference, parametersWith(0.25));
    }
  );
  std::ostringstream ranked;
  writeRankedGlickoPredictions(ranked, 7, 4, 2, matches, reference, parametersWith(0.25));
  require(ranked.str() == expected.str(), "The given reference ratios must be used");
  // α>0で基準とずれていれば、ヘッダーは同じで予測は変わる。
  require(ranked.str() != glicko.str(), "Predictions must change");
  require(ranked.str().substr(0, ranked.str().find('\n')) ==
          "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins", "Incorrect header");
  std::ostringstream ignored;
  rejects([&] {
    writeRankedGlickoPredictions(ignored, 7, 4, 2, matches, {0.5, 0.5}, parametersWith(0.25));
  });
}

}  // namespace

int main() {
  try {
    testBands();
    testBandRatios();
    testDistributionMultiplier();
    testRankedUpdate();
    testPreUpdateBand();
    testInvalidParameters();
    testReadReferenceRatios();
    testWritePredictions();
    std::cout << "Ranked Glicko unit tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
