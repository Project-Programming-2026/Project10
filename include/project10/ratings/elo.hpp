#pragma once

#include "project10/simulation/matches.hpp"

#include <string>
#include <vector>

namespace project10::ratings {

// Eloレーティングのハイパーパラメータ。
// 出典: Arpad E. Elo, "The Rating of Chessplayers, Past and Present" (1978) / FIDE標準ロジスティック式。
struct EloParameters {
  double initialRating = 1500.0;
  double kFactor = 32.0;
  double scale = 400.0;
};

// 2人の推定レートから、playerAがplayerBに勝つ予測確率 E_A = 1 / (1 + 10^((R_B - R_A) / scale)) を返す。
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

// 観測した対戦履歴を先頭から順に毎試合更新し、各人evaluationIntervalMatchesPerPlayer試合ごとの
// チェックポイントで i < j の全ペアの予測勝率をCSV（elo.csv形式）へ出力する。
void writeEloPredictions(
  int runID,
  const std::vector<int>& players,
  const std::vector<Match>& matches,
  int evaluationIntervalMatchesPerPlayer,
  const std::string& outputPath,
  const EloParameters& params = EloParameters{}
);

}  // namespace project10::ratings
