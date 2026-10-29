#pragma once

// ranked_glicko（Glicko初代版）。Glickoの通常の更新量に、所属レート帯の分布倍率を掛ける。
// 式：r' = r + D(b) × (r̂ − r)、D(b) = 1 + α・e / (e + sensitivity²)、e = (P(b) − Q(b))²。
// 出典：チームの提案ノート「Glicko更新量の有界補正」（2026-10-26）の②分布倍率。
// ①基本倍率・③意外さ倍率は、最初は分布倍率だけで始めるというチームの合意により使わない。
// Pは期間の更新前の全員のレート、Qは別シードの基準runの最終Glickoレートから作る帯ごとの
// 人数割合（ranked_glicko2（PR #28）と同じ扱い。未来の試合結果を使わない）。
// RDは通常のGlickoの更新値をそのまま引き継ぐ（提案ノート7.1）。

#include "project10/ratings/glicko.hpp"

#include <istream>
#include <ostream>
#include <vector>

// レート帯の数。950未満、950〜2050の100刻み（11帯）、2050以上の13帯。
constexpr int rankedGlickoBandCount = 13;

// 分布倍率の定数。ranked_glicko2（PR #25・#28）と同じ暫定値で、今後調整する。
constexpr double rankedGlickoAlpha = 0.25;        // 最大の上乗せ（提案ノートのα）
constexpr double rankedGlickoSensitivity = 0.01;  // 感度（提案ノートのc。Glickoのcとは別）

// ranked_glickoの設定。Glickoの値と、分布倍率の値をまとめる。
struct RankedGlickoParameters {
  double initialRating;  // 未評価のレート（Glickoのinitial_rating）
  double maxRD;          // 未評価のRD。期間ごとのRD増加の上限（Glickoのinitial_rd）
  double rdGrowth;       // 期間ごとのRD増加（Glickoのc）
  double alpha = rankedGlickoAlpha;
  double sensitivity = rankedGlickoSensitivity;  // 割合の単位
};

// レートが属する帯の番号（0〜12）を返す。帯6が1450以上1550未満。
int rankedGlickoBand(double rating);

// 全員のレートから、帯ごとの人数割合（合計1）を求める。
std::vector<double> calculateBandRatios(const std::vector<GlickoState>& states);

// 分布倍率D = 1 + α・e / (e + sensitivity²)。2つの割合は同じ帯の値。
double calculateDistributionMultiplier(
  double currentRatio,
  double referenceRatio,
  double alpha,
  double sensitivity
);

// 1期間分を通常のGlickoで更新し、各人のレートの更新量に、更新前の所属帯の分布倍率を掛ける。
// referenceRatiosは基準の帯ごとの人数割合Q（13帯、合計1）。
std::vector<GlickoState> updateRankedGlickoPeriod(
  const std::vector<GlickoState>& states,
  const std::vector<Match>& periodMatches,
  const std::vector<double>& referenceRatios,
  const RankedGlickoParameters& parameters
);

// 基準runの最終レート（project10_glickoが書くglicko_rating.csvの形式）を読み、
// 帯ごとの人数割合Qを返す。1行目は「rating」、2行目以降は有限なレートを1行に1つ。
std::vector<double> readReferenceRatios(std::istream& input);

// writeGlickoPredictionsと同じ形式で、ranked_glickoの予測勝率を書く。
// referenceRatiosは基準の帯ごとの人数割合Q（13帯、合計1）。
void writeRankedGlickoPredictions(
  std::ostream& output,
  int runID,
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  const std::vector<double>& referenceRatios,
  const RankedGlickoParameters& parameters
);
