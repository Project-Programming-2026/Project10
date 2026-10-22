#include "project10/evaluation/mse.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace project10::evaluation {

MseEvaluator::MseEvaluator(
  int runID, int matchesPerPlayer, int interval, ProbabilityMatrix groundTruth
) : runID_(runID), interval_(interval), groundTruth_(std::move(groundTruth)) {
  const auto n = groundTruth_.size();
  if (runID < 0 || interval <= 0 || matchesPerPlayer < interval || n < 2 ||
      n > std::numeric_limits<std::size_t>::max() / n) {
    throw std::invalid_argument("評価設定または勝率行列の大きさが不正です。");
  }
  for (std::size_t a = 0; a < n; a++) {
    if (groundTruth_[a].size() != n) {
      throw std::invalid_argument("正解勝率は正方行列で指定してください。");
    }
    for (std::size_t b = a + 1; b < n; b++) {
      const double p = groundTruth_[a][b];
      if (!std::isfinite(p) || p < 0.0 || p > 1.0) {
        throw std::invalid_argument("正解勝率に欠落または不正な値があります。");
      }
    }
  }
  pairCount_ = n * (n - 1) / 2;
  const auto checkpoints = static_cast<std::size_t>(matchesPerPlayer / interval);
  seen_.assign(checkpoints, std::vector<bool>(n * n, false));
  counts_.assign(checkpoints, 0);
  sums_.assign(checkpoints, 0.0);
  corrections_.assign(checkpoints, 0.0);
}

void MseEvaluator::addPrediction(const PredictionRow& row) {
  if (row.runID != runID_ || row.matchesPerPlayer <= 0 ||
      row.matchesPerPlayer % interval_ != 0 ||
      static_cast<std::size_t>(row.matchesPerPlayer / interval_) > seen_.size()) {
    throw std::invalid_argument("予測データのrun IDまたは評価時点が設定と一致しません。");
  }
  const auto n = groundTruth_.size();
  if (row.playerA < 0 || row.playerB <= row.playerA ||
      static_cast<std::size_t>(row.playerB) >= n) {
    throw std::invalid_argument("予測のペアには既知のIDを使い、player_a < player_bを満たしてください。");
  }
  if (!std::isfinite(row.probability) || row.probability < 0.0 || row.probability > 1.0) {
    throw std::invalid_argument("予測勝率は0以上1以下の有限な値で指定してください。");
  }
  const auto checkpoint = static_cast<std::size_t>(row.matchesPerPlayer / interval_ - 1);
  const auto pair = static_cast<std::size_t>(row.playerA) * n + row.playerB;
  if (seen_[checkpoint][pair]) {
    throw std::invalid_argument("同じ評価時点の予測ペアが重複しています。");
  }
  seen_[checkpoint][pair] = true;
  counts_[checkpoint]++;

  // 第7回会議の定義: 全i<jの(予測勝率-真の勝率)^2を平均する。
  // sums_に合計、corrections_に丸めのずれを保持し、次の項から補正値を引く方法で誤差の蓄積を抑える。
  const double difference = row.probability - groundTruth_[row.playerA][row.playerB];
  const double term = difference * difference - corrections_[checkpoint];
  const double sum = sums_[checkpoint] + term;
  corrections_[checkpoint] = (sum - sums_[checkpoint]) - term;
  sums_[checkpoint] = sum;
}

std::vector<MseResult> MseEvaluator::finish() const {
  std::vector<MseResult> results;
  for (std::size_t t = 0; t < sums_.size(); t++) {
    if (counts_[t] != pairCount_) {
      throw std::invalid_argument("予測ペアが不足しています。評価時点（各人の累積対戦数）: " +
                                  std::to_string((t + 1) * interval_));
    }
    results.push_back({static_cast<int>((t + 1) * interval_), sums_[t] / pairCount_});
  }
  return results;
}

}  // namespace project10::evaluation
