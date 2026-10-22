#include "project10/ratings/elo.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace project10::ratings {
namespace {

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
    throw std::invalid_argument("Self match is not allowed");
  }
  return calculateEloWinProbability(
    ratings_[playerA],
    ratings_[playerB],
    params_.scale
  );
}

// 1試合の結果（resultA: 1=勝ち, 0=負け）をもとに点数を更新する
void EloRating::updateMatch(int playerA, int playerB, int resultA) {
  validatePlayerID(playerA);
  validatePlayerID(playerB);
  if (playerA == playerB) {
    throw std::invalid_argument("Self match is not allowed");
  }
  if (resultA != 0 && resultA != 1) {
    throw std::invalid_argument("resultA must be 0 or 1");
  }

  // 1. Aが勝つ予想確率を計算
  const double expectedA = predictWinProbability(playerA, playerB);

  // 2. 変動量を計算: K * (実際の結果 - 予想確率)
  const double delta = params_.kFactor * (resultA - expectedA);

  // 3. レートを更新（Aが増えた分、Bを減らす）
  ratings_[playerA] += delta;
  ratings_[playerB] -= delta;

  // 4. 試合数を1増やす
  matchCounts_[playerA]++;
  matchCounts_[playerB]++;
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

// 全試合を順番に処理し、定期的に予測勝率をCSVファイルに書き出す
void writeEloPredictions(
  int runID,
  const std::vector<int>& players,
  const std::vector<Match>& matches,
  int evaluationIntervalMatchesPerPlayer,
  const std::string& outputPath,
  const EloParameters& params
) {
  if (runID < 0 || evaluationIntervalMatchesPerPlayer <= 0 || players.empty()) {
    throw std::invalid_argument("Invalid arguments for writeEloPredictions");
  }

  const int totalPlayers = static_cast<int>(players.size());
  const int matchesPerRound = totalPlayers / 2;

  // Eloの計算機を用意
  EloRating elo(players, params);

  // 保存先CSVファイルを開く
  std::ofstream file(outputPath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + outputPath);
  }

  // 1行目のヘッダーを書く
  file << "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n";
  file << std::setprecision(10);

  // 1試合ずつ順番にレートを更新していく
  for (std::size_t i = 0; i < matches.size(); ++i) {
    elo.updateMatch(matches[i]);

    // 全員が1試合終了したか判定
    if ((i + 1) % matchesPerRound == 0) {
      const int currentMatches = static_cast<int>((i + 1) / matchesPerRound);

      // 指定された試合数に到達したら、全ペアの勝率を出力
      if (currentMatches % evaluationIntervalMatchesPerPlayer == 0) {
        for (int a = 0; a < totalPlayers; ++a) {
          for (int b = a + 1; b < totalPlayers; ++b) {
            file << runID << ','
                 << currentMatches << ','
                 << a << ','
                 << b << ','
                 << elo.predictWinProbability(a, b) << '\n';
          }
        }
      }
    }
  }

  file.close();
  if (!file) {
    throw std::runtime_error("Failed to write: " + outputPath);
  }
}

}  // namespace project10::ratings
