#pragma once

#include "project10/ratings/elo.hpp"
#include "project10/simulation/matches.hpp"

#include <vector>

namespace project10::ratings {

// Advantage-Elo（v0）のハイパーパラメータ。
// 基本のElo設定（initialRating=1500, kFactor=32, scale=400; Elo 1978）に加え、
// 全プレイヤー共通の条件効果 a（単位: Eloポイント）の初期値と実効学習率 K_a を持つ。
// 着想の参考:
// - 藤井慧 (2022)「非対称な対戦データを用いたスキルとアドバンテージの同時最尤推定」
// - 高尾海州・松崎公紀 (2023)「初期局面の有利不利に適応したレーティング手法の検証」
// ※オンライン更新式 a' = a + K_a * z * (y - p_A) は本プロジェクトのv0設計案であり、
//   K_a は対数尤度勾配 ∂logL/∂a = (ln(10)/scale) * z * (y - p_A) の定数係数を吸収した実効学習率。
//   K_a = 0.0 を指定すると条件効果 a の学習を無効化できる（最終実験設定は比較前に別途固定する）。
struct AdvantageEloParameters {
  double initialRating = 1500.0;
  double kFactor = 32.0;
  double scale = 400.0;
  double initialAdvantage = 0.0;
  double kAdvantage = 1.0;
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

// 1対1・引き分けなし・毎試合更新のAdvantage-Elo（v0）レーティング推定器。
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
};

}  // namespace project10::ratings
