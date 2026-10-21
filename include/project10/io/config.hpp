#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace project10::io {

// 生成器が使用する設定。未実装機能の設定もsourceTextにそのまま保持する。
struct SimulationConfig {
  int runID;
  int numPlayers;
  double mean;
  double stddev;
  std::uint64_t abilitySeed;
  std::string playersFile;
  std::string latentParametersFile;
  std::string sourceText;
  double performanceStd;
  std::string groundTruthFile;
  int matchesPerPlayer;
  int ratingPeriodMatchesPerPlayer;
  std::uint64_t matchingSeed;
  std::uint64_t resultSeed;
  std::string matchesFile;
};

struct EvaluationConfig {
  int runID;
  int numPlayers;
  int matchesPerPlayer;
  int interval;
  std::string predictionsFile;
  std::string mseFile;
};

// configs/ではなく、生成結果に保存した設定を読む。
EvaluationConfig loadEvaluationConfig(int runID, const std::string& algorithm);

std::string runDirectoryName(int runID);
SimulationConfig loadConfig(int runID);
void writeConfigSnapshot(
  const SimulationConfig& config,
  const std::filesystem::path& outputDir
);

}  // namespace project10::io
