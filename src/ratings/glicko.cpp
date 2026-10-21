// Glicko（初代）の期間更新と勝率予測。
// 目的と数式の出典はglicko.hppを参照。

#include "project10/ratings/glicko.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {

constexpr double pi = 3.14159265358979323846;
// q = ln(10) / 400。10を底とするレート尺度を自然対数の尺度へ変換する。
constexpr double q = 2.30258509299404568402 / 400.0;

// Step 2で使う、1人分の試合の集計値。
struct GameSums {
  bool played = false;
  double inverseDSquared = 0.0;   // 1/d^2 = q^2 * Σ g(RDj)^2 * E * (1 - E)
  double weightedSurprise = 0.0;  // Σ g(RDj) * (sj - E)
};

bool isValidState(const GlickoState& state) {
  return std::isfinite(state.rating) && std::isfinite(state.rd) && state.rd > 0.0;
}

bool isValidPlayer(int playerID, std::size_t numPlayers) {
  return playerID >= 0 && static_cast<std::size_t>(playerID) < numPlayers;
}

// g(RD)：相手のレートが不確かなほど、その試合の重みを下げる。
double g(double rd) {
  return 1.0 / std::sqrt(1.0 + 3.0 * q * q * rd * rd / (pi * pi));
}

// E(s | r, rj, RD)：相手に対する期待スコア。
// Step 2では相手のRD、予測では両者を合成したRDを渡す。
double expectedScore(double rating, double opponentRating, double rd) {
  return 1.0 / (1.0 + std::pow(10.0, -g(rd) * (rating - opponentRating) / 400.0));
}

// 1試合分をプレイヤーの集計値へ加える。
void addGame(
  GameSums& sums,
  const GlickoState& player,
  const GlickoState& opponent,
  double score
) {
  const double weight = g(opponent.rd);
  const double expected = expectedScore(player.rating, opponent.rating, opponent.rd);
  sums.played = true;
  sums.inverseDSquared += q * q * weight * weight * expected * (1.0 - expected);
  sums.weightedSurprise += weight * (score - expected);
}

}  // namespace

std::vector<GlickoState> initializeGlickoStates(
  int numPlayers,
  double initialRating,
  double initialRD
) {
  if (numPlayers <= 0 || !isValidState({initialRating, initialRD})) {
    throw std::invalid_argument("Invalid Glicko initial parameters");
  }
  return std::vector<GlickoState>(static_cast<std::size_t>(numPlayers),
                                  {initialRating, initialRD});
}

std::vector<GlickoState> updateGlickoPeriod(
  const std::vector<GlickoState>& states,
  const std::vector<Match>& periodMatches,
  double initialRD,
  double rdGrowth
) {
  // 途中まで計算した結果を返さないよう、先にすべての入力を検証する。
  if (states.empty() ||
      !std::isfinite(initialRD) || initialRD <= 0.0 ||
      !std::isfinite(rdGrowth) || rdGrowth < 0.0 ||
      !std::all_of(states.begin(), states.end(), isValidState)) {
    throw std::invalid_argument("Invalid Glicko period parameters");
  }
  // 空の期間（全員がStep 1だけ）も受け付けるため、front()は試合があるときだけ参照する。
  const int periodID = periodMatches.empty() ? 0 : periodMatches.front().periodID;
  for (const Match& match : periodMatches) {
    if (!isValidPlayer(match.playerA, states.size()) ||
        !isValidPlayer(match.playerB, states.size()) ||
        match.playerA == match.playerB ||
        (match.resultA != 0 && match.resultA != 1) ||
        match.periodID != periodID) {
      throw std::invalid_argument("Invalid match in Glicko rating period");
    }
  }

  // Step 1(b)：期間の間にRDを増やす。ただし未評価のRDを上限とする。
  std::vector<GlickoState> onsetStates = states;
  for (GlickoState& state : onsetStates) {
    state.rd = std::min(std::hypot(state.rd, rdGrowth), initialRD);
  }

  // 各試合を両者それぞれに1回ずつ、相手の期間開始時の状態で集計する。
  std::vector<GameSums> sums(states.size());
  for (const Match& match : periodMatches) {
    const GlickoState& stateA = onsetStates[match.playerA];
    const GlickoState& stateB = onsetStates[match.playerB];
    addGame(sums[match.playerA], stateA, stateB, match.resultA);
    addGame(sums[match.playerB], stateB, stateA, 1.0 - match.resultA);
  }

  // Step 2：r' = r + q / (1/RD^2 + 1/d^2) * Σ、RD' = sqrt(1 / (1/RD^2 + 1/d^2))。
  // 1/d^2を直接集計するため、Eがすべて0か1でも0除算にならない。
  std::vector<GlickoState> nextStates = onsetStates;
  for (std::size_t i = 0; i < nextStates.size(); i++) {
    if (!sums[i].played) {
      continue;  // 対戦しなかったプレイヤーはStep 1の状態のまま。
    }
    const double precision = 1.0 / (onsetStates[i].rd * onsetStates[i].rd) +
      sums[i].inverseDSquared;
    nextStates[i] = {
      onsetStates[i].rating + q / precision * sums[i].weightedSurprise,
      std::sqrt(1.0 / precision)
    };
    if (!isValidState(nextStates[i])) {
      throw std::runtime_error("Glicko update produced a non-finite value");
    }
  }
  return nextStates;
}

double calculateGlickoWinProbability(
  const GlickoState& stateA,
  const GlickoState& stateB
) {
  if (!isValidState(stateA) || !isValidState(stateB)) {
    throw std::invalid_argument("Invalid Glicko state");
  }
  // E = 1 / (1 + 10^(-g(sqrt(RDA^2 + RDB^2)) * (rA - rB) / 400)).
  return expectedScore(stateA.rating, stateB.rating, std::hypot(stateA.rd, stateB.rd));
}
