#include "project10/evaluation/ranking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace project10::evaluation {
namespace {
std::size_t pairIndex(std::size_t a, std::size_t b, std::size_t n) { return a * (2 * n - a - 1) / 2 + (b - a - 1); }
void kahanAdd(double& sum, double& corr, double value) {
  const double term = value - corr, next = sum + term; corr = (next - sum) - term; sum = next;
}
}  // namespace

RankingEvaluator::RankingEvaluator(int runID, int matchesPerPlayer, int interval,
                                   std::vector<double> latentParameters, bool conditionalPredictions)
    : runID_(runID), interval_(interval), conditional_(conditionalPredictions), latentParameters_(std::move(latentParameters)) {
  const auto n = latentParameters_.size();
  if (runID < 0 || interval <= 0 || matchesPerPlayer < interval || n < 2 || n > std::numeric_limits<std::size_t>::max() / n)
    throw std::invalid_argument("評価設定または潜在能力の大きさが不正です。");
  for (const double theta : latentParameters_)
    if (!std::isfinite(theta)) throw std::invalid_argument("潜在能力に非有限値が含まれています。");
  double weightCorr = 0.0;
  for (std::size_t i = 0; i < n; ++i) for (std::size_t j = i + 1; j < n; ++j) {
    const double diff = std::abs(latentParameters_[i] - latentParameters_[j]);
    if (!std::isfinite(diff)) throw std::invalid_argument("潜在能力の差が有限値の範囲を超えています。");
    kahanAdd(totalWeight_, weightCorr, diff);
  }
  if (!std::isfinite(totalWeight_) || totalWeight_ <= 0.0)
    throw std::invalid_argument("潜在能力が全員同一のため、正規化重み付きKendall距離を計算できません。");
  pairCount_ = n * (n - 1) / 2;
  const auto cps = static_cast<std::size_t>(matchesPerPlayer / interval);
  pairProbabilities_.assign(cps, std::vector<double>(pairCount_, 0.0));
  seenPositive_.assign(cps, std::vector<bool>(pairCount_, false));
  countsPositive_.assign(cps, 0);
  if (conditional_) { seenNegative_.assign(cps, std::vector<bool>(pairCount_, false)); countsNegative_.assign(cps, 0); }
}

void RankingEvaluator::validateRow(const PredictionRow& row) const {
  if (row.runID != runID_ || row.matchesPerPlayer <= 0 || row.matchesPerPlayer % interval_ != 0 ||
      static_cast<std::size_t>(row.matchesPerPlayer / interval_) > pairProbabilities_.size())
    throw std::invalid_argument("予測データのrun IDまたは評価時点が設定と一致しません。");
  if (row.playerA < 0 || row.playerB <= row.playerA || static_cast<std::size_t>(row.playerB) >= latentParameters_.size())
    throw std::invalid_argument("予測のペアには既知のIDを使い、player_a < player_bを満たしてください。");
  if (!std::isfinite(row.probability) || row.probability < 0.0 || row.probability > 1.0)
    throw std::invalid_argument("予測勝率は0以上1以下の有限な値で指定してください。");
}

void RankingEvaluator::addPrediction(const PredictionRow& row) {
  if (conditional_) throw std::invalid_argument("条件z付き予測にはaddConditionalPredictionを使用してください。");
  validateRow(row);
  const auto cp = static_cast<std::size_t>(row.matchesPerPlayer / interval_ - 1);
  const auto idx = pairIndex(static_cast<std::size_t>(row.playerA), static_cast<std::size_t>(row.playerB), latentParameters_.size());
  if (seenPositive_[cp][idx]) throw std::invalid_argument("同じ評価時点の予測ペアが重複しています。");
  seenPositive_[cp][idx] = true; ++countsPositive_[cp]; pairProbabilities_[cp][idx] = row.probability;
}

void RankingEvaluator::addConditionalPrediction(const PredictionRow& row, int z) {
  if (!conditional_) throw std::invalid_argument("通常予測にはaddPredictionを使用してください。");
  if (z != 1 && z != -1) throw std::invalid_argument("条件zには1または-1を指定してください。");
  validateRow(row);
  const auto cp = static_cast<std::size_t>(row.matchesPerPlayer / interval_ - 1);
  const auto idx = pairIndex(static_cast<std::size_t>(row.playerA), static_cast<std::size_t>(row.playerB), latentParameters_.size());
  auto& seen = (z == 1) ? seenPositive_ : seenNegative_; auto& counts = (z == 1) ? countsPositive_ : countsNegative_;
  if (seen[cp][idx]) throw std::invalid_argument("同じ評価時点・条件の予測ペアが重複しています。");
  seen[cp][idx] = true; ++counts[cp]; pairProbabilities_[cp][idx] += 0.5 * row.probability;
}

std::vector<RankingResult> RankingEvaluator::finish() const {
  const auto n = latentParameters_.size(); std::vector<RankingResult> results; results.reserve(pairProbabilities_.size());
  for (std::size_t t = 0; t < pairProbabilities_.size(); ++t) {
    if (countsPositive_[t] != pairCount_ || (conditional_ && countsNegative_[t] != pairCount_))
      throw std::invalid_argument("予測ペアが不足しています。評価時点（各人の累積対戦数）: " + std::to_string((t + 1) * interval_));
    const auto& probs = pairProbabilities_[t]; std::vector<double> scores(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
      double corr = 0.0;
      for (std::size_t j = 0; j < n; ++j)
        if (j != i) kahanAdd(scores[i], corr, (i < j) ? probs[pairIndex(i, j, n)] : (1.0 - probs[pairIndex(j, i, n)]));
    }
    double kendall = 0.0, kendallCorr = 0.0, weighted = 0.0, weightedCorr = 0.0;
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = i + 1; j < n; ++j) {
      const double dTheta = latentParameters_[i] - latentParameters_[j], dScore = scores[i] - scores[j];
      double indicator = 0.5;
      if ((dTheta > 0.0 && dScore < 0.0) || (dTheta < 0.0 && dScore > 0.0)) indicator = 1.0;
      else if ((dTheta > 0.0 && dScore > 0.0) || (dTheta < 0.0 && dScore < 0.0) || (dTheta == 0.0 && dScore == 0.0)) indicator = 0.0;
      kahanAdd(kendall, kendallCorr, indicator);
      if (indicator > 0.0) kahanAdd(weighted, weightedCorr, std::abs(dTheta) * indicator);
    }
    results.push_back({static_cast<int>((t + 1) * interval_), kendall, weighted, std::clamp(weighted / totalWeight_, 0.0, 1.0)});
  }
  return results;
}
}  // namespace project10::evaluation
