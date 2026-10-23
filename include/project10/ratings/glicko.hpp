#pragma once

// Glicko（初代、Glicko-2ではない）。対戦履歴からレートを推定し、勝率を予測する。
// 入力はプレイヤーIDと勝敗のみ。潜在能力・真の勝率は使わない。
// 出典：Glickman「The Glicko system」（https://www.glicko.net/glicko/glicko.pdf）。
// 更新期間は各人10試合（第7回会議）。matches.csvのperiod_idで表す。

#include "project10/simulation/matches.hpp"

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
void writeGlickoPredictions(
  std::ostream& output,
  int runID,
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  double initialRating,
  double initialRD,
  double rdGrowth
);
