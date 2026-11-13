#include "project10/simulation/matches.hpp"
#include "project10/simulation/probit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

std::vector<Match> generateMatches(
  const std::vector<double>& parameters,
  int matchesPerPlayer,
  int ratingPeriodMatchesPerPlayer,
  double performanceStd,
  std::uint64_t matchingSeed,
  std::uint64_t resultSeed,
  double sideAdvantage, std::uint64_t conditionSeed, bool conditional
) {
  if (parameters.size() < 2 || parameters.size() % 2 != 0 ||
      parameters.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      matchesPerPlayer <= 0 || ratingPeriodMatchesPerPlayer <= 0 ||
      !std::isfinite(performanceStd) || performanceStd <= 0.0 ||
      !std::isfinite(sideAdvantage) || sideAdvantage < 0 || (!conditional && sideAdvantage != 0) ||
      !std::all_of(parameters.begin(), parameters.end(), [](double value) {
        return std::isfinite(value);
      })) {
    throw std::invalid_argument("Invalid match generation parameters");
  }

  std::vector<Match> matches;
  const auto matchesPerRound = parameters.size() / 2;
  const auto rounds = static_cast<std::size_t>(matchesPerPlayer);
  if (matchesPerRound > matches.max_size() / rounds) {
    throw std::length_error("Too many matches");
  }
  matches.reserve(matchesPerRound * rounds);
  std::vector<int> players(parameters.size());
  std::iota(players.begin(), players.end(), 0);
  // 一度だけシードを設定する。勝敗の乱数消費が次の対戦相手へ影響しない。
  std::mt19937_64 matchingRng(matchingSeed);
  std::mt19937_64 resultRng(resultSeed);
  std::mt19937_64 conditionRng(conditionSeed);
  std::bernoulli_distribution firstSide(0.5);
  for (int round = 0; round < matchesPerPlayer; round++) {
    // 議事録の「ランダムに選択」を具体化する案。再戦は許すが自己対戦はない。
    // プレイヤーの配列をシャッフルして、隣り合う2人を対戦させる。
    std::shuffle(players.begin(), players.end(), matchingRng);
    for (std::size_t i = 0; i < players.size(); i += 2) {
      const int condition = conditional ? (firstSide(conditionRng) ? 1 : -1) : 1;
      const double probability = calculateConditionalWinProbability(
        parameters[players[i]], parameters[players[i + 1]], performanceStd, sideAdvantage, condition
      );
      std::bernoulli_distribution result(probability);
      matches.push_back({
        static_cast<std::uint64_t>(matches.size()), round / ratingPeriodMatchesPerPlayer,
        players[i], players[i + 1], result(resultRng) ? 1 : 0, condition
      });
    }
  }
  return matches;
}
