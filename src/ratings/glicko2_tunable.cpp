// 調整実験用Glicko-2の期間更新と予測。目的と出典はglicko2_tunable.hppを参照。

#include "project10/ratings/glicko2_tunable.hpp"

#include "project10/ratings/glicko.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace project10::ratings {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double unratedRating = 1500.0;  // Step 1(a)・Step 2の中心
constexpr double scaleFactor = 173.7178;  // Step 2・8のレート尺度とGlicko-2尺度の比（原典の値）
constexpr double tolerance = 0.000001;    // Step 5.1の収束判定ε（原典の値）
// 原典の1万回の試算ではStep 5.4の反復は最大19回。これを大きく超えたら収束失敗とする。
constexpr int maxIterations = 1000;

bool isPositiveFinite(double value) {
  return std::isfinite(value) && value > 0.0;
}

bool isValidState(const Glicko2State& state) {
  return std::isfinite(state.rating) && isPositiveFinite(state.ratingDeviation) &&
    isPositiveFinite(state.volatility);
}

// Step 3のg(φ)。引数はGlicko-2尺度のRD。
double g(double scaledDeviation) {
  return 1.0 / std::sqrt(1.0 + 3.0 * scaledDeviation * scaledDeviation / (pi * pi));
}

// Step 3のE(μ, μj, φj)。レートと相手のレート・RDはGlicko-2尺度。
double expectedScore(double scaledRating, double opponentScaledRating,
                     double opponentScaledDeviation) {
  return 1.0 / (1.0 + std::exp(-g(opponentScaledDeviation) * (scaledRating - opponentScaledRating)));
}

// Step 5：Illinois法で新しいボラティリティσ'を求める。
// estimatedVarianceはStep 3のv、estimatedImprovementはStep 4のΔ。
double updateVolatility(double volatility, double scaledDeviation, double estimatedVariance,
                        double estimatedImprovement, double tau) {
  const double a = std::log(volatility * volatility);
  const double phiSquared = scaledDeviation * scaledDeviation;
  const double deltaSquared = estimatedImprovement * estimatedImprovement;
  const auto f = [&](double x) {
    const double denominator = phiSquared + estimatedVariance + std::exp(x);
    return std::exp(x) * (deltaSquared - phiSquared - estimatedVariance - std::exp(x)) /
      (2.0 * denominator * denominator) - (x - a) / (tau * tau);
  };

  double lower = a;  // 原典のA
  double upper;      // 原典のB
  if (deltaSquared > phiSquared + estimatedVariance) {
    upper = std::log(deltaSquared - phiSquared - estimatedVariance);
  } else {
    int k = 1;
    while (f(a - k * tau) < 0.0) {
      if (++k > maxIterations) {
        throw std::runtime_error("Glicko-2 volatility bracket was not found");
      }
    }
    upper = a - k * tau;
  }
  double fLower = f(lower);
  double fUpper = f(upper);
  for (int iteration = 0; std::abs(upper - lower) > tolerance; iteration++) {
    if (iteration == maxIterations) {
      throw std::runtime_error("Glicko-2 volatility iteration did not converge");
    }
    const double next = lower + (lower - upper) * fLower / (fUpper - fLower);  // 原典のC
    const double fNext = f(next);
    if (fNext * fUpper <= 0.0) {
      lower = upper;
      fLower = fUpper;
    } else {
      fLower /= 2.0;
    }
    upper = next;
    fUpper = fNext;
  }
  return std::exp(lower / 2.0);
}

// 1人分の対戦相手と得点。相手の値は期間開始時のGlicko-2尺度。
struct Game {
  double opponentScaledRating;
  double opponentScaledDeviation;
  double score;  // 自分の勝ち1・負け0
};

// 1人分のStep 2〜8。
Glicko2State updatePlayer(const Glicko2State& state, const std::vector<Game>& games, double tau) {
  const double scaledRating = (state.rating - unratedRating) / scaleFactor;
  const double scaledDeviation = state.ratingDeviation / scaleFactor;
  if (games.empty()) {
    // 対戦しなかった人はStep 6だけを適用し、RDだけを増やす。
    return {state.rating, scaleFactor * std::hypot(scaledDeviation, state.volatility),
            state.volatility};
  }

  double inverseVariance = 0.0;   // Step 3のvの逆数
  double weightedSurprise = 0.0;  // Step 4・7のΣ g(φj){sj − E}
  for (const Game& game : games) {
    const double weight = g(game.opponentScaledDeviation);
    const double expected = expectedScore(scaledRating, game.opponentScaledRating,
                                          game.opponentScaledDeviation);
    inverseVariance += weight * weight * expected * (1.0 - expected);
    weightedSurprise += weight * (game.score - expected);
  }
  if (!isPositiveFinite(inverseVariance)) {
    throw std::runtime_error("Glicko-2 variance is not finite");
  }
  const double estimatedVariance = 1.0 / inverseVariance;
  const double volatility = updateVolatility(state.volatility, scaledDeviation, estimatedVariance,
                                             estimatedVariance * weightedSurprise, tau);
  const double deviationBeforeGames = std::hypot(scaledDeviation, volatility);  // Step 6のφ*
  const double nextScaledDeviation =
    1.0 / std::sqrt(1.0 / (deviationBeforeGames * deviationBeforeGames) + inverseVariance);
  const double nextScaledRating =
    scaledRating + nextScaledDeviation * nextScaledDeviation * weightedSurprise;
  const Glicko2State next{scaleFactor * nextScaledRating + unratedRating,
                          scaleFactor * nextScaledDeviation, volatility};
  if (!isValidState(next)) {
    throw std::runtime_error("Glicko-2 update produced a non-finite value");
  }
  return next;
}

}  // namespace

std::vector<Glicko2State> initializeGlicko2States(
  int numPlayers,
  const Glicko2Parameters& parameters
) {
  const Glicko2State unrated{unratedRating, parameters.initialRatingDeviation,
                             parameters.initialVolatility};
  if (numPlayers <= 0 || !isValidState(unrated) || !isPositiveFinite(parameters.tau)) {
    throw std::invalid_argument("Invalid Glicko-2 initial parameters");
  }
  return std::vector<Glicko2State>(static_cast<std::size_t>(numPlayers), unrated);
}

std::vector<Glicko2State> updateGlicko2Period(
  const std::vector<Glicko2State>& statesAtPeriodStart,
  const std::vector<Match>& periodMatches,
  double tau
) {
  // 添字に使う前に、すべての入力を検証する。
  const auto numPlayers = statesAtPeriodStart.size();
  const auto isKnown = [numPlayers](int id) {
    return id >= 0 && static_cast<std::size_t>(id) < numPlayers;
  };
  if (numPlayers == 0 || !isPositiveFinite(tau) ||
      !std::all_of(statesAtPeriodStart.begin(), statesAtPeriodStart.end(), isValidState)) {
    throw std::invalid_argument("Invalid Glicko-2 period parameters");
  }
  for (const Match& match : periodMatches) {
    if (!isKnown(match.playerA) || !isKnown(match.playerB) || match.playerA == match.playerB ||
        (match.resultA != 0 && match.resultA != 1) ||
        match.periodID != periodMatches.front().periodID) {
      throw std::invalid_argument("Invalid match in Glicko-2 rating period");
    }
  }

  std::vector<std::vector<Game>> games(numPlayers);
  for (const Match& match : periodMatches) {
    const Glicko2State& stateA = statesAtPeriodStart[match.playerA];
    const Glicko2State& stateB = statesAtPeriodStart[match.playerB];
    games[match.playerA].push_back({(stateB.rating - unratedRating) / scaleFactor,
                                    stateB.ratingDeviation / scaleFactor,
                                    static_cast<double>(match.resultA)});
    games[match.playerB].push_back({(stateA.rating - unratedRating) / scaleFactor,
                                    stateA.ratingDeviation / scaleFactor,
                                    1.0 - match.resultA});
  }
  std::vector<Glicko2State> next(numPlayers);
  for (std::size_t player = 0; player < numPlayers; player++) {
    next[player] = updatePlayer(statesAtPeriodStart[player], games[player], tau);
  }
  return next;
}

double calculateGlicko2WinProbability(
  const Glicko2State& playerA,
  const Glicko2State& playerB
) {
  if (!isValidState(playerA) || !isValidState(playerB)) {
    throw std::invalid_argument("Invalid Glicko-2 state");
  }
  return calculateGlickoWinProbability({playerA.rating, playerA.ratingDeviation},
                                       {playerB.rating, playerB.ratingDeviation});
}

void predictGlicko2AtCheckpoints(
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  const Glicko2Parameters& parameters,
  const Glicko2PredictionConsumer& consume
) {
  if (interval <= 0) {
    throw std::invalid_argument("Invalid Glicko-2 evaluation interval");
  }
  auto states = initializeGlicko2States(numPlayers, parameters);
  std::vector<int> counts(states.size(), 0);  // 各人の累積対戦数
  int completed = 0;  // 直前の期間終了時点の累積対戦数
  for (std::size_t begin = 0; begin < matches.size();) {
    std::size_t end = begin;
    while (end < matches.size() && matches[end].periodID == matches[begin].periodID) {
      end++;
    }
    const std::vector<Match> period(matches.begin() + begin, matches.begin() + end);
    states = updateGlicko2Period(states, period, parameters.tau);  // IDと勝敗も検証する。
    for (const Match& match : period) {
      counts[match.playerA]++;
      counts[match.playerB]++;
    }
    const int played = counts.front();
    if (!std::all_of(counts.begin(), counts.end(), [played](int count) { return count == played; })) {
      throw std::invalid_argument("Match counts differ at the end of a Glicko-2 period");
    }
    // 評価時点が期間の途中にあると、更新を終えた状態で予測できない。
    if ((played - 1) / interval > completed / interval) {
      throw std::invalid_argument("An evaluation point falls inside a Glicko-2 period");
    }
    completed = played;
    if (completed % interval == 0) {
      for (int a = 0; a < numPlayers; a++) {
        for (int b = a + 1; b < numPlayers; b++) {
          consume(completed, a, b, calculateGlicko2WinProbability(states[a], states[b]));
        }
      }
    }
    begin = end;
  }
}

}  // namespace project10::ratings
