#pragma once

#include "project10/simulation/matches.hpp"
#include "project10/evaluation/mse.hpp"

#include <string>
#include <vector>

void writePlayers(const std::vector<int>& players, const std::string& outputPath);
void writeMatches(const std::vector<Match>& matches, const std::string& outputPath);
void writeLatentParameters(
  const std::vector<double>& parameters,
  const std::string& outputPath
);

void writeGroundTruth(
  const std::vector<double>& parameters,
  double performanceStd,
  const std::string& outputPath
);

namespace project10::io {

void validatePlayersCsv(const std::string& path, int numPlayers);
evaluation::ProbabilityMatrix readGroundTruthCsv(const std::string& path, int numPlayers);
void readPredictionsCsv(const std::string& path, evaluation::MseEvaluator& evaluator);
void writeMseCsv(
  const std::string& path, int runID, const std::vector<evaluation::MseResult>& results
);

}  // namespace project10::io
