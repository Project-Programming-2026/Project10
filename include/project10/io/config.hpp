#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "project10/ratings/advantage_elo.hpp"

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
  bool conditional = false;
  std::string scenario;
  double sideAdvantage = 0;
  std::uint64_t conditionSeed = 0;
};

struct EvaluationConfig {
  int runID;
  int numPlayers;
  int matchesPerPlayer;
  int interval;
  std::string predictionsFile;
  std::string mseFile;
  bool conditional = false;
};

struct RatingConfig {
  int numPlayers;
  int matchesPerPlayer;
  int interval;
  int period;
  std::string predictionsFile;
  bool conditional = false;
};

RatingConfig loadTrueSkillConfig(int runID);
// rootをconfigsにすると、生成前に同じ検証を適用できる。
RatingConfig loadRatingConfig(int runID, const std::string& algorithm,
                              const std::filesystem::path& root = "runs");

// Glicko（初代）の実行設定。初期値とc（rd_growth）は設定のglicko項目から読む。
struct GlickoConfig {
  RatingConfig rating;
  double initialRating;
  double initialRD;
  double rdGrowth;
};

GlickoConfig loadGlickoConfig(int runID, const std::filesystem::path& root = "runs");

// configs/ではなく、生成結果に保存した設定を読む。
EvaluationConfig loadEvaluationConfig(int runID, const std::string& algorithm,
                                      const std::filesystem::path& root = "runs");

struct PlotConfig {
  int runID;
  int matchesPerPlayer;
  int interval;
  std::map<std::string, std::string> mseFiles;
  std::string scenario;
};

// 描画には正解情報や予測CSVを必要としない。
PlotConfig loadPlotConfig(int runID, const std::filesystem::path& root = "runs");

std::string runDirectoryName(int runID);
SimulationConfig loadConfig(int runID, const std::filesystem::path& root = "configs");
SimulationConfig loadSimulationConfigFile(const std::filesystem::path& path);
bool isFixedReferenceRun(int runID);
// 作成元マーカーと、現在の設定・保存済み設定の意味上の一致を検査する。
void validateRunSnapshot(int runID, bool allMethods = false, bool comparison = false, int referenceRun = -1);
std::vector<std::string> ratingMethods(bool allMethods = false, bool ranked = true);
ratings::AdvantageEloParameters loadAdvantageParameters(int runID, const std::filesystem::path& root = "runs");
void writeRatingSnapshot(int runID, const std::filesystem::path& directory);
int comparisonReferenceRun(int runID);
void writeConfigSnapshot(
  const SimulationConfig& config,
  const std::filesystem::path& outputDir
);

}  // namespace project10::io
