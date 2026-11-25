#pragma once

#include "project10/ratings/elo.hpp"
#include "project10/simulation/matches.hpp"

#include <vector>

namespace project10::ratings {

// 条件効果 a の更新規則。
enum class AdvantageUpdateMethod {
  // 固定の更新幅による更新（v0既定）: a' = a + kAdvantage * z * (y - p_A)
  FixedStep,
  // 試合から得られた情報量 I_t を積み上げ、ズレを I_t で割ることで更新幅を自動調整する（オンラインNewton法）:
  //   I_t = rho * I_{t-1} + beta^2 * z^2 * p_A * (1 - p_A),  a' = a + beta * z * (y - p_A) / I_t
  //   ただし beta = ln(10) / scale、rho = onlineNewtonForgettingFactor ∈ (0, 1]、
  //   初期値 I_0 = onlineNewtonPriorMatches * beta^2 * 0.25
  OnlineNewton
};

// Advantage-Elo（v0およびオンラインNewton拡張）のハイパーパラメータ。
// 基本のElo設定（initialRating=1500, kFactor=32, scale=400; Elo 1978）に加え、
// 全プレイヤー共通の条件効果 a（単位: Eloポイント）の初期値・更新規則・設定値を持つ。
// 着想の参考:
// - 藤井慧 (2022)「非対称な対戦データを用いたスキルとアドバンテージの同時最尤推定」
// - 高尾海州・松崎公紀 (2023)「初期局面の有利不利に適応したレーティング手法の検証」
// ※FixedStepの a' = a + K_a * z * (y - p_A) は本プロジェクトのv0設計案であり、
//   K_a は対数尤度の傾き ∂logL/∂a = (ln(10)/scale) * z * (y - p_A) の定数係数をまとめた更新幅。
//   K_a = 0.0 を指定するとFixedStep時は条件効果 a の学習を無効化できる（最終実験設定は比較前に別途固定する）。
//   OnlineNewtonでは、試合ごとに積み上げた情報量 I_t で傾きを割ることで更新幅を自動で小さくする。
struct AdvantageEloParameters {
  double initialRating = 1500.0;
  double kFactor = 32.0;
  double scale = 400.0;
  double initialAdvantage = 0.0;
  double kAdvantage = 1.0;
  AdvantageUpdateMethod advantageUpdate = AdvantageUpdateMethod::FixedStep;
  // 最初のブレーキ役となる仮想の事前試合数（正の有限値が必要）。
  double onlineNewtonPriorMatches = 20.0;
  // 古い試合の重みを少しずつ減らす忘却係数 rho ∈ (0, 1]。1.0 で忘れない。
  double onlineNewtonForgettingFactor = 1.0;
};

// 2人の推定レートと条件効果 a（Eloポイント単位）、先手側フラグ z ∈ {+1, -1} から、
// playerAがplayerBに勝つ試合前予測確率 p_A = 1 / (1 + 10^(-(R_A - R_B + a * z) / scale)) を返す。
// z = +1 は playerA が条件を与えられる側（先手）、z = -1 は playerB が条件を与えられる側を表す。
double calculateAdvantageEloWinProbability(
  double ratingA,
  double ratingB,
  double advantage,
  int z,
  double scale = 400.0
);

// 1対1・引き分けなし・毎試合更新のAdvantage-Eloレーティング推定器。
// 個人の基礎実力（各プレイヤーのレート R_i）と全プレイヤー共通の条件効果 a を分離して推定する。
// 潜在能力や正解勝率は受け取らず、プレイヤーID・先手条件 z・観測した勝敗のみから状態を更新する。
class AdvantageEloRating {
public:
  explicit AdvantageEloRating(
    int numPlayers,
    const AdvantageEloParameters& params = AdvantageEloParameters{}
  );
  explicit AdvantageEloRating(
    const std::vector<int>& players,
    const AdvantageEloParameters& params = AdvantageEloParameters{}
  );

  int numPlayers() const noexcept;
  const AdvantageEloParameters& parameters() const noexcept;

  double rating(int playerID) const;
  const std::vector<double>& ratings() const noexcept;

  int matchCount(int playerID) const;
  const std::vector<int>& matchCounts() const noexcept;

  // 現在推定されている全プレイヤー共通の条件効果 a（Eloポイント単位）を返す。
  double advantage() const noexcept;

  // OnlineNewton で積み上げている情報量 I（FixedStep 時は初期値 I_0）を返す。
  double advantageInfo() const noexcept;

  // 試合前の推定レートと条件効果 a に基づき、条件 z ∈ {+1, -1} のもとで
  // playerAがplayerBに勝つ予測確率 p_A（0〜1）を返す。
  double predictWinProbability(int playerA, int playerB, int z) const;

  // 1試合の結果（resultA: A勝利=1, A敗北=0）と先手条件 z ∈ {+1, -1} を観測し、
  // 旧レート・旧 a に基づく同一の試合前予測勝率 p_A を使って R_A, R_B, a を同時更新する。
  void updateMatch(int playerA, int playerB, int resultA, int z);
  void updateMatch(const Match& match, int z);

private:
  AdvantageEloParameters params_;
  EloRating baseElo_;
  double advantage_;
  double advantageInfo_;
};

}  // namespace project10::ratings
