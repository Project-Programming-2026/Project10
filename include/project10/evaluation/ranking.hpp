#pragma once

#include "project10/evaluation/mse.hpp"
#include <cstddef>
#include <vector>

namespace project10::evaluation {
struct RankingResult {
  int matchesPerPlayer;
  double kendallDistance, weightedKendallDistance, normalizedWeightedKendallDistance;
};
class RankingEvaluator {
public:
  RankingEvaluator(int runID, int matchesPerPlayer, int interval,
                   std::vector<double> latentParameters, bool conditionalPredictions = false);
  void addPrediction(const PredictionRow& row);
  void addConditionalPrediction(const PredictionRow& row, int z);
  std::vector<RankingResult> finish() const;

private:
  void validateRow(const PredictionRow& row) const;
  int runID_, interval_;
  bool conditional_;
  std::vector<double> latentParameters_;
  double totalWeight_ = 0.0;
  std::size_t pairCount_ = 0;
  std::vector<std::vector<double>> pairProbabilities_;
  std::vector<std::vector<bool>> seenPositive_, seenNegative_;
  std::vector<std::size_t> countsPositive_, countsNegative_;
};
}  // namespace project10::evaluation
