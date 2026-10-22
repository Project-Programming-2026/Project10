#include "project10/ratings/elo.hpp"

#include <cmath>
#include <stdexcept>

namespace project10::ratings {
namespace {

// パラメータが正常な有限値（かつ正の値）であることを確認する
void validateParameters(const EloParameters& params) {
  if (!std::isfinite(params.initialRating)) {
    throw std::invalid_argument("initialRating must be finite");
  }
  if (!std::isfinite(params.kFactor) || params.kFactor <= 0.0) {
    throw std::invalid_argument("kFactor must be positive and finite");
  }
  if (!std::isfinite(params.scale) || params.scale <= 0.0) {
    throw std::invalid_argument("scale must be positive and finite");
  }
}

}  // namespace

// 2人のレートから「Aが勝つ確率」を計算する
// 公式: E_A = 1 / (1 + 10^((ratingB - ratingA) / scale))
double calculateEloWinProbability(
  double ratingA,
  double ratingB,
  double scale
) {
  if (!std::isfinite(scale) || scale <= 0.0) {
    throw std::invalid_argument("scale must be positive and finite");
  }
  if (!std::isfinite(ratingA) || !std::isfinite(ratingB)) {
    throw std::invalid_argument("ratings must be finite");
  }
  if (ratingA == ratingB) {
    return 0.5;
  }

  const double diff = ratingB - ratingA;
  const double exponent = diff / scale;

  // レート差が極端に大きい場合、10^exponent のオーバーフローを防ぐためにクリッピング
  // double型の10進数表現の上限は約308乗なので、300を超えたら安全に0.0または1.0を返す
  if (exponent > 300.0) {
    return 0.0;
  }
  if (exponent < -300.0) {
    return 1.0;
  }

  return 1.0 / (1.0 + std::pow(10.0, exponent));
}

// 人数と設定を指定して初期化する
EloRating::EloRating(int numPlayers, const EloParameters& params)
  : params_(params) {
  if (numPlayers < 2) {
    throw std::invalid_argument("numPlayers must be at least 2");
  }
  validateParameters(params_);

  // 全員を初期レートで初期化
  ratings_.assign(numPlayers, params_.initialRating);
  // 全員の対戦数を0試合で初期化
  matchCounts_.assign(numPlayers, 0);
}

// プレイヤーID一覧を指定して初期化する
EloRating::EloRating(
  const std::vector<int>& players,
  const EloParameters& params
) : params_(params) {
  if (players.size() < 2) {
    throw std::invalid_argument("players must contain at least 2 IDs");
  }
  validateParameters(params_);

  // IDが0から順番に並んでいるか確認
  for (std::size_t i = 0; i < players.size(); ++i) {
    if (players[i] != static_cast<int>(i)) {
      throw std::invalid_argument("Player IDs must be consecutive starting from 0");
    }
  }

  ratings_.assign(players.size(), params_.initialRating);
  matchCounts_.assign(players.size(), 0);
}

int EloRating::numPlayers() const noexcept {
  return static_cast<int>(ratings_.size());
}

const EloParameters& EloRating::parameters() const noexcept {
  return params_;
}

double EloRating::rating(int playerID) const {
  validatePlayerID(playerID);
  return ratings_[playerID];
}

const std::vector<double>& EloRating::ratings() const noexcept {
  return ratings_;
}

int EloRating::matchCount(int playerID) const {
  validatePlayerID(playerID);
  return matchCounts_[playerID];
}

const std::vector<int>& EloRating::matchCounts() const noexcept {
  return matchCounts_;
}

// 今のレートをもとに「AがBに勝つ確率」を予想する
double EloRating::predictWinProbability(int playerA, int playerB) const {
  validatePlayerID(playerA);
  validatePlayerID(playerB);
  if (playerA == playerB) {
    throw std::invalid_argument("A player cannot play against themselves");
  }
  return calculateEloWinProbability(ratings_[playerA], ratings_[playerB], params_.scale);
}

// 試合結果をもとに、2人のレートを更新する
void EloRating::updateMatch(int playerA, int playerB, int resultA) {
  validatePlayerID(playerA);
  validatePlayerID(playerB);
  if (playerA == playerB) {
    throw std::invalid_argument("A player cannot play against themselves");
  }
  if (resultA != 0 && resultA != 1) {
    throw std::invalid_argument("resultA must be 1 (win) or 0 (loss)");
  }

  // 1. 試合前のレートから「Aが勝つ確率」を計算
  const double expectedA = predictWinProbability(playerA, playerB);
  const double actualA = static_cast<double>(resultA);

  // 2. 公式に従って変動量を計算: Δ = K * (実結果 - 予想勝率)
  const double delta = params_.kFactor * (actualA - expectedA);

  // 3. レートを更新（Aが増えた分だけBは減る）
  const double newRatingA = ratings_[playerA] + delta;
  const double newRatingB = ratings_[playerB] - delta;

  // 4. 更新後のレートが有限値であることを確認（極端な値の防御）
  if (!std::isfinite(newRatingA) || !std::isfinite(newRatingB)) {
    throw std::runtime_error("Rating update resulted in a non-finite value");
  }

  ratings_[playerA] = newRatingA;
  ratings_[playerB] = newRatingB;

  // 5. 試合数を1増やす
  matchCounts_[playerA] += 1;
  matchCounts_[playerB] += 1;
}

void EloRating::updateMatch(const Match& match) {
  updateMatch(match.playerA, match.playerB, match.resultA);
}

// プレイヤーIDが有効範囲（0 〜 人数-1）に入っているか確認する
void EloRating::validatePlayerID(int playerID) const {
  if (playerID < 0 || static_cast<std::size_t>(playerID) >= ratings_.size()) {
    throw std::invalid_argument("Unknown player ID");
  }
}

}  // namespace project10::ratings
