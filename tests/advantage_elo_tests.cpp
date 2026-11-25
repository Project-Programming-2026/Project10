#include "project10/ratings/advantage_elo.hpp"
#include "project10/ratings/elo.hpp"
#include "project10/simulation/probit.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
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

// 5. オンラインNewton更新の手計算照合・対称性・スケール不変性
void testOnlineNewtonHandCalculationAndProperties() {
  constexpr double tolerance = 1e-12;
  const double scale = 400.0;
  const double beta = std::log(10.0) / scale;
  const double priorMatches = 20.0;

  project10::ratings::AdvantageEloParameters params;
  params.advantageUpdate = project10::ratings::AdvantageUpdateMethod::OnlineNewton;
  params.onlineNewtonPriorMatches = priorMatches;

  project10::ratings::AdvantageEloRating elo(2, params);

  // 初期状態: I_0 = 20 * beta^2 * 0.25 = 5 * beta^2
  const double info0 = priorMatches * beta * beta * 0.25;
  require(std::abs(elo.advantageFisherInfo() - info0) < tolerance,
          "Initial Fisher info must equal priorMatches * beta^2 * 0.25");

  // 1戦目: R_0=R_1=1500, a=0, z=+1, y=1 -> p_1 = 0.5
  elo.updateMatch(0, 1, 1, 1);
  const double info1 = info0 + beta * beta * 0.25;
  const double adv1 = (beta * 0.5) / info1;
  const double r0After1 = 1500.0 + 32.0 * 0.5;
  const double r1After1 = 1500.0 - 32.0 * 0.5;
  require(std::abs(elo.advantageFisherInfo() - info1) < tolerance,
          "Fisher info after match 1 must match hand calculation");
  require(std::abs(elo.advantage() - adv1) < tolerance,
          "Advantage after match 1 must match hand calculation");
  require(std::abs(elo.rating(0) - r0After1) < tolerance &&
          std::abs(elo.rating(1) - r1After1) < tolerance,
          "Player ratings after match 1 must match standard Elo step");

  // 2戦目: z=-1（player 1が先手）, y=0（player 1が勝利）
  const double p2 =
    project10::ratings::calculateAdvantageEloWinProbability(r0After1, r1After1, adv1, -1, scale);
  elo.updateMatch(0, 1, 0, -1);
  const double info2 = info1 + beta * beta * p2 * (1.0 - p2);
  const double adv2 = adv1 + (beta * (-1.0) * (0.0 - p2)) / info2;
  require(std::abs(elo.advantageFisherInfo() - info2) < tolerance,
          "Fisher info after match 2 must match hand calculation");
  require(std::abs(elo.advantage() - adv2) < tolerance,
          "Advantage after match 2 must match hand calculation");

  // 対称性・スケール不変性:
  // (a) A/B交換・z反転・結果反転（同じ試合をB視点で観測）では、a, I_t, 各プレイヤーレートが一致する
  project10::ratings::AdvantageEloRating swapped(2, params);
  swapped.updateMatch(1, 0, 0, -1);
  swapped.updateMatch(1, 0, 1, 1);
  require(std::abs(swapped.advantage() - elo.advantage()) < tolerance,
          "OnlineNewton advantage must be invariant under swapping A/B and flipping z, result");
  require(std::abs(swapped.advantageFisherInfo() - elo.advantageFisherInfo()) < tolerance,
          "OnlineNewton Fisher info must be invariant under swapping A/B and flipping z, result");
  require(std::abs(swapped.rating(0) - elo.rating(0)) < tolerance &&
          std::abs(swapped.rating(1) - elo.rating(1)) < tolerance,
          "OnlineNewton ratings must match when viewing the same match from B's side");

  // (b) プレイヤー0と1の役割（勝敗と先手後手）を入れ替えた場合は、aとI_tが一致し、0と1のレートが入れ替わる
  project10::ratings::AdvantageEloRating mirrored(2, params);
  mirrored.updateMatch(0, 1, 0, -1);
  mirrored.updateMatch(0, 1, 1, 1);
  require(std::abs(mirrored.advantage() - elo.advantage()) < tolerance,
          "OnlineNewton advantage must be invariant when swapping player roles");
  require(std::abs(mirrored.advantageFisherInfo() - elo.advantageFisherInfo()) < tolerance,
          "OnlineNewton Fisher info must be invariant when swapping player roles");
  require(std::abs(mirrored.rating(0) - elo.rating(1)) < tolerance &&
          std::abs(mirrored.rating(1) - elo.rating(0)) < tolerance,
          "OnlineNewton ratings must swap symmetrically when player roles swap");

  // (c) scaleとKを2倍にした場合、a も厳密に2倍になる（scale不変性）
  project10::ratings::AdvantageEloParameters scaledParams = params;
  scaledParams.scale = 800.0;
  scaledParams.kFactor = 64.0;
  project10::ratings::AdvantageEloRating scaled(2, scaledParams);
  scaled.updateMatch(0, 1, 1, 1);
  scaled.updateMatch(0, 1, 0, -1);
  require(std::abs(scaled.advantage() - 2.0 * elo.advantage()) < tolerance,
          "OnlineNewton advantage must scale linearly with Elo scale");

  // (d) 忘却係数 rho = 0.9 のとき I_1 = 0.9 * I_0 + beta^2 * 0.25 になる手計算照合
  project10::ratings::AdvantageEloParameters forgetParams = params;
  forgetParams.onlineNewtonForgettingFactor = 0.9;
  project10::ratings::AdvantageEloRating forgetElo(2, forgetParams);
  forgetElo.updateMatch(0, 1, 1, 1);
  const double expectedForgetInfo1 = 0.9 * info0 + beta * beta * 0.25;
  const double expectedForgetAdv1 = (beta * 0.5) / expectedForgetInfo1;
  require(std::abs(forgetElo.advantageFisherInfo() - expectedForgetInfo1) < tolerance,
          "Fisher info with forgetting factor must match hand calculation");
  require(std::abs(forgetElo.advantage() - expectedForgetAdv1) < tolerance,
          "Advantage with forgetting factor must match hand calculation");
}

// 6. OnlineNewtonの収束性・忘却係数の効果確認
void testOnlineNewtonConvergenceOnSyntheticMatches() {
  // (a) 既知レート下でのロジスティックモデル復元確認（rho = 1.0）
  {
    constexpr int numPlayers = 50;
    constexpr int numMatches = 1500;
    constexpr double trueAdvantage = 150.0;

    std::mt19937_64 rng(2026111300ULL);
    std::uniform_int_distribution<int> playerDist(0, numPlayers - 1);
    std::bernoulli_distribution sideDist(0.5);
    std::uniform_real_distribution<double> uniformDist(0.0, 1.0);

    // 個人レートをほぼ動かさず（極小K）、同実力対戦から条件効果 a 単体の復元を検証する
    project10::ratings::AdvantageEloParameters params;
    params.kFactor = 1e-6;
    params.advantageUpdate = project10::ratings::AdvantageUpdateMethod::OnlineNewton;
    params.onlineNewtonPriorMatches = 20.0;
    params.onlineNewtonForgettingFactor = 1.0;

    project10::ratings::AdvantageEloRating estimator(numPlayers, params);
    for (int m = 0; m < numMatches; ++m) {
      const int a = playerDist(rng);
      int b = playerDist(rng);
      if (a == b) {
        b = (a + 1) % numPlayers;
      }
      const int z = sideDist(rng) ? 1 : -1;
      const double pTrue = project10::ratings::calculateAdvantageEloWinProbability(
        1500.0, 1500.0, trueAdvantage, z, 400.0
      );
      const int y = (uniformDist(rng) < pTrue) ? 1 : 0;
      estimator.updateMatch(a, b, y, z);
    }
    require(std::abs(estimator.advantage() - trueAdvantage) < 25.0,
            "Pure OnlineNewton must recover true logistic advantage when ratings are settled");
  }

  // (b) S1相当のプロビット合成対戦列（200人・200ラウンド）での同時学習と忘却係数の効果
  constexpr int numPlayers = 200;
  constexpr int rounds = 200;
  constexpr double sideAdvantage = 1.0;
  constexpr double performanceStd = 1.0;

  std::mt19937_64 abilityRng(2026111301ULL);
  std::normal_distribution<double> abilityDist(0.0, 1.0);
  std::vector<double> theta(numPlayers);
  for (double& value : theta) {
    value = abilityDist(abilityRng);
  }

  std::mt19937_64 matchRng(2026111302ULL);
  std::bernoulli_distribution sideDist(0.5);
  std::uniform_real_distribution<double> uniformDist(0.0, 1.0);

  project10::ratings::AdvantageEloParameters pureParams;
  pureParams.advantageUpdate = project10::ratings::AdvantageUpdateMethod::OnlineNewton;
  pureParams.onlineNewtonPriorMatches = 20.0;
  pureParams.onlineNewtonForgettingFactor = 1.0;
  project10::ratings::AdvantageEloRating pureNewton(numPlayers, pureParams);

  project10::ratings::AdvantageEloParameters forgetParams = pureParams;
  forgetParams.onlineNewtonForgettingFactor = 0.9995;
  project10::ratings::AdvantageEloRating forgetNewton(numPlayers, forgetParams);

  double earlyPureAdvantage = 0.0;
  double earlyForgetAdvantage = 0.0;
  std::vector<int> order(numPlayers);
  std::iota(order.begin(), order.end(), 0);
  for (int r = 0; r < rounds; ++r) {
    std::shuffle(order.begin(), order.end(), matchRng);
    for (int i = 0; i < numPlayers; i += 2) {
      const int a = order[i];
      const int b = order[i + 1];
      const int z = sideDist(matchRng) ? 1 : -1;
      const double pTrue = calculateWinProbability(theta[a] + sideAdvantage * z, theta[b], performanceStd);
      const int y = (uniformDist(matchRng) < pTrue) ? 1 : 0;
      pureNewton.updateMatch(a, b, y, z);
      forgetNewton.updateMatch(a, b, y, z);
    }
    if (r == 1) {
      earlyPureAdvantage = pureNewton.advantage();
      earlyForgetAdvantage = forgetNewton.advantage();
    }
  }

  require(std::isfinite(pureNewton.advantage()) && std::isfinite(pureNewton.advantageFisherInfo()),
          "Pure OnlineNewton state must remain finite after 20,000 synthetic matches");
  require(std::isfinite(forgetNewton.advantage()) && std::isfinite(forgetNewton.advantageFisherInfo()),
          "Forgetting OnlineNewton state must remain finite after 20,000 synthetic matches");
  require(earlyPureAdvantage > 50.0 && earlyForgetAdvantage > 50.0,
          "OnlineNewton must learn substantial positive advantage within the first 2 rounds");
  require(pureNewton.advantageFisherInfo() > 20.0 * std::pow(std::log(10.0) / 400.0, 2) * 0.25,
          "Cumulative Fisher information must grow over matches");
  require(forgetNewton.advantage() > 150.0 && forgetNewton.advantage() < 400.0,
          "Forgetting OnlineNewton must track positive side advantage in a valid Elo range");
  require(forgetNewton.advantage() > pureNewton.advantage(),
          "Forgetting factor must avoid early-round step-size lock-in while ratings spread");
}

// 7. 異常系の網羅確認
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
  rejects([&] {
    Params p;
    p.advantageUpdate = static_cast<project10::ratings::AdvantageUpdateMethod>(99);
    Estimator elo(2, p);
  });
  rejects([&] { Params p; p.onlineNewtonPriorMatches = 0.0; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonPriorMatches = -5.0; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonPriorMatches = nan; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonPriorMatches = inf; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonForgettingFactor = 0.0; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonForgettingFactor = -0.5; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonForgettingFactor = 1.01; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonForgettingFactor = nan; Estimator elo(2, p); });
  rejects([&] { Params p; p.onlineNewtonForgettingFactor = inf; Estimator elo(2, p); });
  rejects([&] { Params p; p.scale = 1e300; Estimator elo(2, p); });

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
    testOnlineNewtonHandCalculationAndProperties();
    testOnlineNewtonConvergenceOnSyntheticMatches();
    testInvalidInputs();
    std::cout << "Advantage-Elo単体確認に成功しました" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "テスト失敗: " << error.what() << std::endl;
    return 1;
  }
}
