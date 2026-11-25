#include "project10/ratings/advantage_elo.hpp"

#include <cmath>
#include <stdexcept>

namespace project10::ratings {
namespace {

EloParameters toBaseEloParameters(const AdvantageEloParameters& params) {
  if (!std::isfinite(params.initialAdvantage)) {
    throw std::invalid_argument("initialAdvantage must be finite");
  }
  if (!std::isfinite(params.kAdvantage) || params.kAdvantage < 0.0) {
    throw std::invalid_argument("kAdvantage must be non-negative and finite");
  }
  if (params.advantageUpdate != AdvantageUpdateMethod::FixedStep &&
      params.advantageUpdate != AdvantageUpdateMethod::OnlineNewton) {
    throw std::invalid_argument("advantageUpdate must be FixedStep or OnlineNewton");
  }
  if (!std::isfinite(params.onlineNewtonPriorMatches) || params.onlineNewtonPriorMatches <= 0.0) {
    throw std::invalid_argument("onlineNewtonPriorMatches must be positive and finite");
  }
  if (!std::isfinite(params.onlineNewtonForgettingFactor) ||
      params.onlineNewtonForgettingFactor <= 0.0 ||
      params.onlineNewtonForgettingFactor > 1.0) {
    throw std::invalid_argument("onlineNewtonForgettingFactor must be in (0, 1]");
  }
  return EloParameters{
    params.initialRating,
    params.kFactor,
    params.scale
  };
}

double initialAdvantageInfo(const AdvantageEloParameters& params) {
  const double beta = std::log(10.0) / params.scale;
  // 互角の試合 (p_A = 0.5) 1戦あたりの情報量は beta^2 * 0.25。
  const double info = params.onlineNewtonPriorMatches * beta * beta * 0.25;
  if (!std::isfinite(info) || info <= 0.0) {
    throw std::invalid_argument("onlineNewtonPriorMatches produces invalid initial advantage info");
  }
  return info;
}

void validateCondition(int z) {
  if (z != 1 && z != -1) {
    throw std::invalid_argument("z must be +1 (playerA has condition) or -1 (playerB has condition)");
  }
}

}  // namespace

double calculateAdvantageEloWinProbability(
  double ratingA,
  double ratingB,
  double advantage,
  int z,
  double scale
) {
  if (!std::isfinite(ratingA) || !std::isfinite(ratingB)) {
    throw std::invalid_argument("ratings must be finite");
  }
  if (!std::isfinite(advantage)) {
    throw std::invalid_argument("advantage must be finite");
  }
  validateCondition(z);

  const double effectiveRatingA = ratingA + advantage * static_cast<double>(z);
  if (!std::isfinite(effectiveRatingA)) {
    throw std::invalid_argument("effective ratingA must be finite");
  }
  return calculateEloWinProbability(effectiveRatingA, ratingB, scale);
}

AdvantageEloRating::AdvantageEloRating(
  int numPlayers,
  const AdvantageEloParameters& params
) : params_(params),
    baseElo_(numPlayers, toBaseEloParameters(params)),
    advantage_(params.initialAdvantage),
    advantageInfo_(initialAdvantageInfo(params)) {}

AdvantageEloRating::AdvantageEloRating(
  const std::vector<int>& players,
  const AdvantageEloParameters& params
) : params_(params),
    baseElo_(players, toBaseEloParameters(params)),
    advantage_(params.initialAdvantage),
    advantageInfo_(initialAdvantageInfo(params)) {}

int AdvantageEloRating::numPlayers() const noexcept {
  return baseElo_.numPlayers();
}

const AdvantageEloParameters& AdvantageEloRating::parameters() const noexcept {
  return params_;
}

double AdvantageEloRating::rating(int playerID) const {
  return baseElo_.rating(playerID);
}

const std::vector<double>& AdvantageEloRating::ratings() const noexcept {
  return baseElo_.ratings();
}

int AdvantageEloRating::matchCount(int playerID) const {
  return baseElo_.matchCount(playerID);
}

const std::vector<int>& AdvantageEloRating::matchCounts() const noexcept {
  return baseElo_.matchCounts();
}

double AdvantageEloRating::advantage() const noexcept {
  return advantage_;
}

double AdvantageEloRating::advantageInfo() const noexcept {
  return advantageInfo_;
}

double AdvantageEloRating::predictWinProbability(
  int playerA,
  int playerB,
  int z
) const {
  validateCondition(z);
  const double offsetA = advantage_ * static_cast<double>(z);
  return baseElo_.predictWinProbability(playerA, playerB, offsetA);
}

void AdvantageEloRating::updateMatch(
  int playerA,
  int playerB,
  int resultA,
  int z
) {
  validateCondition(z);
  if (resultA != 0 && resultA != 1) {
    throw std::invalid_argument("resultA must be 1 (win) or 0 (loss)");
  }

  // 1. 旧レート・旧 a に基づく試合前予測勝率 p_A を計算
  const double zVal = static_cast<double>(z);
  const double offsetA = advantage_ * zVal;
  const double expectedA = baseElo_.predictWinProbability(playerA, playerB, offsetA);
  const double actualA = static_cast<double>(resultA);
  const double error = actualA - expectedA;

  // 2. 条件効果 a（OnlineNewton時は累積情報量 I_t も含む）の更新後値を計算し事前検証
  double newAdvantage = advantage_;
  double newInfo = advantageInfo_;
  if (params_.advantageUpdate == AdvantageUpdateMethod::OnlineNewton) {
    const double beta = std::log(10.0) / params_.scale;
    // 互角の試合 (p_A ≈ 0.5) ほど大きくなる情報の重み
    const double matchInfo = beta * beta * zVal * zVal * expectedA * (1.0 - expectedA);
    newInfo = params_.onlineNewtonForgettingFactor * advantageInfo_ + matchInfo;
    if (!std::isfinite(newInfo) || newInfo <= 0.0) {
      throw std::runtime_error("Advantage info update resulted in an invalid value");
    }
    // ズレを累積情報量で割って a を更新
    newAdvantage = advantage_ + (beta * zVal * error) / newInfo;
  } else {
    newAdvantage = advantage_ + params_.kAdvantage * zVal * error;
  }
  if (!std::isfinite(newAdvantage)) {
    throw std::runtime_error("Advantage update resulted in a non-finite value");
  }

  // 3. 同じ試合前予測勝率 p_A に基づいて両者のレートを更新し、同時に a も更新
  (void)baseElo_.updateMatch(playerA, playerB, resultA, offsetA);
  advantage_ = newAdvantage;
  advantageInfo_ = newInfo;
}

void AdvantageEloRating::updateMatch(const Match& match, int z) {
  updateMatch(match.playerA, match.playerB, match.resultA, z);
}

}  // namespace project10::ratings
