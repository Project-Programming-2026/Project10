#pragma once

#include "project10/simulation/matches.hpp"

#include <vector>

namespace project10::ratings {

// Eloレーティングのハイパーパラメータ。
// 出典: Arpad E. Elo, "The Rating of Chessplayers, Past and Present" (1978).
// 初期値1500・Scale=400はElo(1978)および全米チェス連盟(USCF)等で標準的に採用されている値。
// K=32はUSCF等の初期・暫定プレイヤー向け基準値として広く使用される設定。
struct EloParameters {
  double initialRating = 1500.0;
  double kFactor = 32.0;
  double scale = 400.0;
};

// 2人の推定レートから、playerAがplayerBに勝つ予測確率 E_A = 1 / (1 + 10^((R_B - R_A) / scale)) を返す。
// レート差が極端に大きい場合はオーバーフローを避けるため安全にクリッピングを行う。
double calculateEloWinProbability(
  double ratingA,
  double ratingB,
  double scale = 400.0
);

// 1対1・引き分けなし・毎試合更新のEloレーティング推定器。
// 潜在能力や正解勝率は受け取らず、プレイヤーIDと観測した対戦結果のみから状態を更新する。
class EloRating {
public:
  explicit EloRating(int numPlayers, const EloParameters& params = EloParameters{});
  explicit EloRating(
    const std::vector<int>& players,
    const EloParameters& params = EloParameters{}
  );

  int numPlayers() const noexcept;
  const EloParameters& parameters() const noexcept;

  double rating(int playerID) const;
  const std::vector<double>& ratings() const noexcept;

  int matchCount(int playerID) const;
  const std::vector<int>& matchCounts() const noexcept;

  // 現在の推定レートに基づき、playerAがplayerBに勝つ予測確率（0〜1）を返す。
  double predictWinProbability(int playerA, int playerB) const;

  // 1試合の結果（resultA: A勝利=1, A敗北=0）を観測し、試合前の予測勝率を使って両者のレートを同時更新する。
  void updateMatch(int playerA, int playerB, int resultA);
  void updateMatch(const Match& match);

private:
  void validatePlayerID(int playerID) const;

  EloParameters params_;
  std::vector<double> ratings_;
  std::vector<int> matchCounts_;
};

}  // namespace project10::ratings
