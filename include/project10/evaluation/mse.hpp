#pragma once

#include <cstddef>
#include <vector>

namespace project10::evaluation {

// [a][b]はAがBに勝つ確率。評価にはa < bの成分だけを使う。
using ProbabilityMatrix = std::vector<std::vector<double>>;

struct PredictionRow {
  int runID;
  int matchesPerPlayer;
  int playerA;
  int playerB;
  double probability;
};

struct MseResult {
  int matchesPerPlayer;
  double mse;
};

class MseEvaluator {
public:
  MseEvaluator(int runID, int matchesPerPlayer, int interval, ProbabilityMatrix groundTruth);
  void addPrediction(const PredictionRow& row);
  std::vector<MseResult> finish() const;

private:
  int runID_;
  int interval_;
  ProbabilityMatrix groundTruth_;
  std::size_t pairCount_;
  std::vector<std::vector<bool>> seen_;
  std::vector<std::size_t> counts_;
  std::vector<double> sums_;
  std::vector<double> corrections_;
};

}  // namespace project10::evaluation
