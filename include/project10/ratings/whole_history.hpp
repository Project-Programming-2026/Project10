#pragma once

// 全履歴推定：評価時点ごとに、それまでに観測した全試合から全員のレートを推定し直す。
// 能力が変化しないBradley–Terryモデル（Glicko-2尺度のロジスティック）と、全員に共通の
// 正規分布の事前分布による事後最頻値を求め、RDは1人ずつのヘッセ行列から近似する。
// 出典：Coulom「Whole-History Rating」（https://www.remi-coulom.fr/WHR/WHR.pdf、2008年版）の
// 全履歴の事後最頻値とヘッセ行列による不確実性。原典との違い（時間変化なし、正規分布の事前分布、
// Glickoの予測式）はdocs/references.mdに記録する。入力はプレイヤーIDと勝敗のみ。

#include "project10/simulation/matches.hpp"

#include <functional>
#include <vector>

namespace project10::ratings {

// ratingとratingDeviationはGlicko（初代）と同じレート尺度（未評価は1500）。
// ratingDeviationは推定の不確実性で、生成器のperformance_stdとは別物。
struct WholeHistoryEstimate {
  double rating;
  double ratingDeviation;
};

// 観測済みの全試合を保持し、refit()で推定し直す。前回の推定値から計算を始める。
class WholeHistoryEstimator {
public:
  // priorRatingDeviationは全員に共通の事前分布N(1500, priorRatingDeviation²)の標準偏差（レート尺度）。
  WholeHistoryEstimator(int numPlayers, double priorRatingDeviation);

  // 試合を追加する。プレイヤーID・自己対戦・勝敗を検証し、不正なら何も追加しない。
  void addMatches(const std::vector<Match>& matches);

  // 追加済みの全試合で事後最頻値とRDを求め直す。収束しない場合や非有限値は例外にする。
  const std::vector<WholeHistoryEstimate>& refit();

private:
  struct Game {
    int opponent;
    double score;  // 自分の勝ち1・負け0
  };
  double priorPrecision_;            // Glicko-2尺度での事前分布の精度 1/φ0²
  std::vector<std::vector<Game>> games_;
  std::vector<double> scaledRatings_;  // Glicko-2尺度の推定値（前回の解）
  std::vector<WholeHistoryEstimate> estimates_;
};

// AがBに勝つ予測確率。Glicko（初代）の予測式に両者のRDを渡す。
double calculateWholeHistoryWinProbability(
  const WholeHistoryEstimate& playerA,
  const WholeHistoryEstimate& playerB
);

using WholeHistoryPredictionConsumer =
  std::function<void(int matchesPerPlayer, int playerA, int playerB, double probabilityAWins)>;

// 試合順の対戦履歴を先頭から追加し、全員の累積対戦数がintervalの倍数で揃った時点ごとに
// 推定し直して、全a < bの予測勝率をconsumeへ渡す。期間（periodID）は使わない。
// 評価時点で対戦数が揃わない場合と、最後に全員の対戦数が揃わない場合は拒否する。
void predictWholeHistoryAtCheckpoints(
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  double priorRatingDeviation,
  const WholeHistoryPredictionConsumer& consume
);

}  // namespace project10::ratings
