#pragma once

// Glicko（初代、Glicko-2ではない）。対戦履歴からレートを推定し、勝率を予測する。
// 入力はプレイヤーIDと勝敗のみ。潜在能力・真の勝率は使わない。
// 出典：Glickman「The Glicko system」（https://www.glicko.net/glicko/glicko.pdf）。
// 更新期間は各人10試合（第7回会議）。matches.csvのperiod_idで表す。

#include "project10/simulation/matches.hpp"

#include <cstddef>
#include <functional>
#include <ostream>
#include <vector>

// 推定レートとその不確実性RD。生成器のperformance_stdとは別物。
struct GlickoState {
  double rating;
  double rd;
};

// Step 1(a)：全員を未評価の状態にする（原典の既定値は1500・350）。添字がプレイヤーID。
std::vector<GlickoState> initializeGlickoStates(
  int numPlayers,
  double initialRating,
  double initialRD
);

// 1期間分の対戦でStep 1(b)・Step 2を行い、更新後の状態を返す。
// 対戦はすべて同じperiodIDとし、期間開始時の状態で計算する。
// rdGrowthは原典のc。initialRDはRDの上限も兼ねる。
std::vector<GlickoState> updateGlickoPeriod(
  const std::vector<GlickoState>& states,
  const std::vector<Match>& periodMatches,
  double initialRD,
  double rdGrowth
);

// AがBに勝つ確率を、両者のRDを考慮して返す。期間更新後の状態を渡す。
double calculateGlickoWinProbability(
  const GlickoState& stateA,
  const GlickoState& stateB
);

// 試合順の対戦履歴をperiodIDごとに更新し、各人の累積対戦数がintervalの倍数になった
// 期間の終了時に、全a < bの予測勝率を予測CSVの形式でoutputへ書く。
// 期間の途中に評価時点がある場合や、期間の終了時に各人の対戦数が揃わない場合は拒否する。
// 戻り値は最後の期間の更新後の全員の状態（添字がプレイヤーID）。
std::vector<GlickoState> writeGlickoPredictions(
  std::ostream& output,
  int runID,
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  double initialRating,
  double initialRD,
  double rdGrowth
);

// matches[begin]から同じperiodIDが続く範囲を、1期間分として取り出す。
std::vector<Match> takeGlickoPeriod(const std::vector<Match>& matches, std::size_t begin);

// 1期間分の更新方法。更新前の全員の状態と1期間分の対戦から、更新後の状態を返す。
using GlickoPeriodUpdate = std::function<std::vector<GlickoState>(
  const std::vector<GlickoState>& states,
  const std::vector<Match>& periodMatches
)>;

// writeGlickoPredictionsと同じ形式で、期間の更新方法だけをupdatePeriodに差し替えて書く。
// initialStatesは全員の初期状態（添字がプレイヤーID）。ranked_glickoなどの派生方式で使う。
std::vector<GlickoState> writeGlickoPredictionsWith(
  std::ostream& output,
  int runID,
  int interval,
  const std::vector<Match>& matches,
  std::vector<GlickoState> initialStates,
  const GlickoPeriodUpdate& updatePeriod
);
