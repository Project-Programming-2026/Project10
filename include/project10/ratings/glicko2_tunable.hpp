#pragma once

// 収束を速める調整実験用のGlicko-2。τ・初期RD・初期ボラティリティを引数で変えられる。
// 予測CSVを出す既存のsrc/ratings/glicko2.cppは定数が固定のため、そちらは変えずに同じ式をここに持つ。
// 既定値（τ 0.5、RD 350、σ 0.06）で既存実装と同じ予測になることはテストで確認する。
// 出典：Glickman「Example of the Glicko-2 system」2022-03-22版
// （https://www.glicko.net/glicko/glicko2.pdf）のStep 1〜8。
// 入力はプレイヤーIDと勝敗のみ。潜在能力・真の勝率は使わない。

#include "project10/simulation/matches.hpp"

#include <functional>
#include <vector>

namespace project10::ratings {

struct Glicko2Parameters {
  double initialRatingDeviation;  // Step 1(a)のRD。レートと同じ尺度（原典の値は350）
  double initialVolatility;       // Step 1(a)のσ。Glicko-2尺度（原典の値は0.06）
  double tau;                     // Step 1のシステム定数τ。σの変化を抑える（原典の推奨は0.3〜1.2）
};

// ratingとratingDeviationはレート尺度（未評価は1500）、volatilityはGlicko-2尺度。
// ratingDeviationは推定の不確実性で、生成器のperformance_stdとは別物。
struct Glicko2State {
  double rating;
  double ratingDeviation;
  double volatility;
};

// Step 1(a)：全員を未評価の状態にする。添字がプレイヤーID。
std::vector<Glicko2State> initializeGlicko2States(
  int numPlayers,
  const Glicko2Parameters& parameters
);

// 1期間分の対戦でStep 2〜8を行い、期間終了時の状態を返す。対戦はすべて同じperiodIDとし、
// 相手には期間開始時の状態を使う。対戦しなかった人にはStep 6だけを適用する。
std::vector<Glicko2State> updateGlicko2Period(
  const std::vector<Glicko2State>& statesAtPeriodStart,
  const std::vector<Match>& periodMatches,
  double tau
);

// AがBに勝つ予測確率。既存のglicko2.cppと同じく、Glicko（初代）の予測式に両者のRDを渡す。
double calculateGlicko2WinProbability(
  const Glicko2State& playerA,
  const Glicko2State& playerB
);

using Glicko2PredictionConsumer =
  std::function<void(int matchesPerPlayer, int playerA, int playerB, double probabilityAWins)>;

// 試合順の対戦履歴をperiodIDごとに更新する。各人の累積対戦数がintervalの倍数になった期間の
// 終了時に、全a < bの予測勝率をconsumeへ渡す。評価時点が期間の途中にある場合は拒否する。
void predictGlicko2AtCheckpoints(
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  const Glicko2Parameters& parameters,
  const Glicko2PredictionConsumer& consume
);

}  // namespace project10::ratings
