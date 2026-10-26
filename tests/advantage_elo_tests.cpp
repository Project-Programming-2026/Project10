#include "project10/ratings/advantage_elo.hpp"
#include "project10/ratings/elo.hpp"

#include <cmath>
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

// 1. 確率の手計算と交換対称性の確認
void testHandCalculatedProbabilityAndSymmetry() {
  constexpr double tolerance = 1e-12;

  // R_A = R_B = 1500, a = 400, s = 400 のとき:
  // z = +1 なら p_A = 10 / 11、z = -1 なら p_A = 1 / 11
  const double pPlus =
    project10::ratings::calculateAdvantageEloWinProbability(1500.0, 1500.0, 400.0, 1, 400.0);
  const double pMinus =
    project10::ratings::calculateAdvantageEloWinProbability(1500.0, 1500.0, 400.0, -1, 400.0);
  require(std::abs(pPlus - (10.0 / 11.0)) < tolerance,
          "R_A=R_B=1500, a=400, z=+1 must give p_A = 10/11");
  require(std::abs(pMinus - (1.0 / 11.0)) < tolerance,
          "R_A=R_B=1500, a=400, z=-1 must give p_A = 1/11");

  project10::ratings::AdvantageEloParameters params{1500.0, 32.0, 400.0, 400.0, 1.0};
  project10::ratings::AdvantageEloRating rating(2, params);
  require(std::abs(rating.predictWinProbability(0, 1, 1) - (10.0 / 11.0)) < tolerance,
          "Estimator predictWinProbability with z=+1 must match 10/11");
  require(std::abs(rating.predictWinProbability(0, 1, -1) - (1.0 / 11.0)) < tolerance,
          "Estimator predictWinProbability with z=-1 must match 1/11");

  // 交換対称性: A/Bを交換し z を反転した場合、P(A wins) + P(B wins) == 1.0
  params.initialAdvantage = -125.0;
  params.kAdvantage = 4.0;
  project10::ratings::AdvantageEloRating asymmetric(3, params);
  asymmetric.updateMatch(0, 1, 1, 1);
  asymmetric.updateMatch(1, 2, 0, -1);

  for (int z : {1, -1}) {
    const double pAB = asymmetric.predictWinProbability(0, 2, z);
    const double pBA = asymmetric.predictWinProbability(2, 0, -z);
    require(std::abs((pAB + pBA) - 1.0) < tolerance,
            "Swapping A/B and negating z must satisfy P(A wins) + P(B wins) == 1");
  }
}

// 2. 条件効果 a の更新と、補正済み予測勝率によるレート更新の整合確認
void testAdvantageAndRatingSimultaneousUpdate() {
  constexpr double tolerance = 1e-12;

  // (a) a=0, R_A=R_B=1500 (p_A=0.5) のとき z=+1, y=1 なら a は K_a/2 増加し、z=-1 では符号が逆になる
  project10::ratings::AdvantageEloParameters params{1500.0, 32.0, 400.0, 0.0, 2.0};
  project10::ratings::AdvantageEloRating eloPlus(2, params);
  eloPlus.updateMatch(0, 1, 1, 1);
  require(std::abs(eloPlus.advantage() - 1.0) < tolerance,
          "z=+1, y=1, p_A=0.5 must increase advantage by K_a / 2");

  project10::ratings::AdvantageEloRating eloMinus(2, params);
  eloMinus.updateMatch(0, 1, 1, -1);
  require(std::abs(eloMinus.advantage() - (-1.0)) < tolerance,
          "z=-1, y=1, p_A=0.5 must change advantage by -K_a / 2");

  project10::ratings::AdvantageEloRating eloBWin(2, params);
  eloBWin.updateMatch(0, 1, 0, -1);
  require(std::abs(eloBWin.advantage() - 1.0) < tolerance,
          "z=-1, y=0 (first-mover B wins) must increase advantage by K_a / 2");

  // (b) 仕様書の具体例: 両者1500点、s=400、Aが先手(z=+1)、a=400、K=32、K_a=11
  // 試合前予測 p_A = 10/11、A勝利(y=1) のとき予測誤差 e = 1/11。
  // R_A' = 1500 + 32/11, R_B' = 1500 - 32/11, a' = 400 + 11*(1/11) = 401。
  project10::ratings::AdvantageEloParameters specParams{1500.0, 32.0, 400.0, 400.0, 11.0};
  project10::ratings::AdvantageEloRating elo(2, specParams);
  elo.updateMatch(0, 1, 1, 1);

  const double expectedDeltaR = 32.0 * (1.0 / 11.0);
  require(std::abs(elo.rating(0) - (1500.0 + expectedDeltaR)) < tolerance,
          "Winner rating must be updated using condition-adjusted p_A (10/11)");
  require(std::abs(elo.rating(1) - (1500.0 - expectedDeltaR)) < tolerance,
          "Loser rating must be updated using condition-adjusted p_A (10/11)");
  require(std::abs(elo.advantage() - 401.0) < tolerance,
          "Advantage must be updated simultaneously using pre-match p_A");
  require(elo.matchCount(0) == 1 && elo.matchCount(1) == 1,
          "Match counts must be incremented");
}

// 3. 通常Eloとの完全互換性確認（a=0 かつ K_a=0）
void testCompatibilityWithStandardElo() {
  constexpr double tolerance = 1e-12;
  const std::vector<int> players = {0, 1, 2, 3};

  project10::ratings::EloRating stdElo(players, {1500.0, 32.0, 400.0});
  project10::ratings::AdvantageEloRating advElo(players, {1500.0, 32.0, 400.0, 0.0, 0.0});

  struct ConditionedMatch {
    Match match;
    int z;
  };
  const std::vector<ConditionedMatch> history = {
    {{0, 0, 0, 1, 1}, 1},  {{1, 0, 2, 3, 0}, -1}, {{2, 0, 0, 2, 0}, 1},
    {{3, 0, 1, 3, 1}, -1}, {{4, 1, 0, 3, 1}, -1}, {{5, 1, 1, 2, 0}, 1}
  };

  for (const auto& item : history) {
    const double pStd = stdElo.predictWinProbability(item.match.playerA, item.match.playerB);
    const double pAdv = advElo.predictWinProbability(item.match.playerA, item.match.playerB, item.z);
    require(std::abs(pStd - pAdv) < tolerance,
            "Pre-match predictions must match standard Elo when a=0 and K_a=0");
    stdElo.updateMatch(item.match);
    advElo.updateMatch(item.match, item.z);
    require(advElo.advantage() == 0.0, "Advantage must remain 0 when a=0 and K_a=0");
  }

  for (int id : players) {
    require(std::abs(stdElo.rating(id) - advElo.rating(id)) < tolerance,
            "All ratings must match standard Elo when a=0 and K_a=0");
    require(stdElo.matchCount(id) == advElo.matchCount(id),
            "All match counts must match standard Elo");
  }
  for (int a = 0; a < 4; ++a) {
    for (int b = 0; b < 4; ++b) {
      if (a == b) continue;
      require(std::abs(stdElo.predictWinProbability(a, b) -
                       advElo.predictWinProbability(a, b, 1)) < tolerance,
              "All pairwise predictions (z=+1) must match standard Elo");
      require(std::abs(stdElo.predictWinProbability(a, b) -
                       advElo.predictWinProbability(a, b, -1)) < tolerance,
              "All pairwise predictions (z=-1) must match standard Elo");
    }
  }
}

// 4. 小規模結合確認: S1相当の同一試合列から通常EloとAdvantage-Eloの試合前予測を取得・更新できる
void testSequentialPreMatchPredictionLogging() {
  project10::ratings::EloRating stdElo(4);
  project10::ratings::AdvantageEloRating advElo(4, {1500.0, 32.0, 400.0, 0.0, 4.0});

  const std::vector<Match> matches = {
    {0, 0, 0, 1, 1}, {1, 0, 2, 3, 1}, {2, 0, 0, 2, 0}, {3, 0, 1, 3, 0}
  };
  const std::vector<int> conditions = {1, 1, -1, -1}; // 全試合で先手側が勝利

  for (std::size_t i = 0; i < matches.size(); ++i) {
    const double pElo = stdElo.predictWinProbability(matches[i].playerA, matches[i].playerB);
    const double pAdv = advElo.predictWinProbability(matches[i].playerA, matches[i].playerB, conditions[i]);
    require(pElo > 0.0 && pElo < 1.0, "Standard Elo pre-match probability must be in (0, 1)");
    require(pAdv > 0.0 && pAdv < 1.0, "Advantage-Elo pre-match probability must be in (0, 1)");
    stdElo.updateMatch(matches[i]);
    advElo.updateMatch(matches[i], conditions[i]);
  }
  require(advElo.advantage() > 0.0,
          "Learned advantage must become positive when first-mover consistently wins");
}

// 5. 異常系の網羅確認
void testInvalidInputs() {
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double inf = std::numeric_limits<double>::infinity();
  using Params = project10::ratings::AdvantageEloParameters;
  using Estimator = project10::ratings::AdvantageEloRating;

  rejects([&] { Estimator elo(1); });
  rejects([&] { Estimator elo(std::vector<int>{0}); });
  rejects([&] { Estimator elo(std::vector<int>{0, 2}); });
  rejects([&] { Params p; p.initialRating = nan; Estimator elo(2, p); });
  rejects([&] { Params p; p.kFactor = 0.0; Estimator elo(2, p); });
  rejects([&] { Params p; p.scale = -400.0; Estimator elo(2, p); });
  rejects([&] { Params p; p.initialAdvantage = nan; Estimator elo(2, p); });
  rejects([&] { Params p; p.initialAdvantage = inf; Estimator elo(2, p); });
  rejects([&] { Params p; p.kAdvantage = -0.1; Estimator elo(2, p); });
  rejects([&] { Params p; p.kAdvantage = nan; Estimator elo(2, p); });
  rejects([&] { Params p; p.kAdvantage = inf; Estimator elo(2, p); });

  rejects([&] { project10::ratings::calculateAdvantageEloWinProbability(1500.0, 1500.0, 0.0, 0); });
  rejects([&] { project10::ratings::calculateAdvantageEloWinProbability(1500.0, 1500.0, 0.0, 2); });
  rejects([&] { project10::ratings::calculateAdvantageEloWinProbability(1500.0, 1500.0, nan, 1); });
  rejects([&] { project10::ratings::calculateAdvantageEloWinProbability(1500.0, 1500.0, 0.0, 1, 0.0); });

  Estimator elo(2);
  rejects([&] { elo.rating(-1); });
  rejects([&] { elo.rating(2); });
  rejects([&] { elo.matchCount(-1); });
  rejects([&] { elo.predictWinProbability(0, 0, 1); });   // 自己対戦
  rejects([&] { elo.predictWinProbability(-1, 1, 1); });  // 不正ID
  rejects([&] { elo.predictWinProbability(0, 1, 0); });   // z = 0
  rejects([&] { elo.predictWinProbability(0, 1, 2); });   // z = 2
  rejects([&] { elo.updateMatch(0, 0, 1, 1); });          // 自己対戦
  rejects([&] { elo.updateMatch(0, 2, 1, 1); });          // 不正ID
  rejects([&] { elo.updateMatch(0, 1, 2, 1); });          // resultA = 2
  rejects([&] { elo.updateMatch(0, 1, -1, 1); });         // resultA = -1
  rejects([&] { elo.updateMatch(0, 1, 1, 0); });          // z = 0
  rejects([&] { elo.updateMatch(0, 1, 1, -2); });         // z = -2
}

}  // namespace

int main() {
  try {
    testHandCalculatedProbabilityAndSymmetry();
    testAdvantageAndRatingSimultaneousUpdate();
    testCompatibilityWithStandardElo();
    testSequentialPreMatchPredictionLogging();
    testInvalidInputs();
    std::cout << "Advantage-Elo単体確認に成功しました" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "テスト失敗: " << error.what() << std::endl;
    return 1;
  }
}
