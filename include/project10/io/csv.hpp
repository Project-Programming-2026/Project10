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
std::vector<Match> readMatchesCsv(
  const std::string& path, int numPlayers, int matchesPerPlayer, int period
);
evaluation::ProbabilityMatrix readGroundTruthCsv(const std::string& path, int numPlayers);
void readPredictionsCsv(const std::string& path, evaluation::MseEvaluator& evaluator);
std::vector<evaluation::MseResult> readMseCsv(
  const std::string& path, int runID, int matchesPerPlayer, int interval
);
void writeMseCsv(
  const std::string& path, int runID, const std::vector<evaluation::MseResult>& results
);
// 保存済み一時ファイルを同一ファイルシステム内のハードリンクで公開する。
// 出力先があれば原子的に失敗する。成功後の一時ファイル削除は呼び出し側で行う。
void publishMseCsv(const std::string& temporaryPath, const std::string& outputPath);

}  // namespace project10::io
