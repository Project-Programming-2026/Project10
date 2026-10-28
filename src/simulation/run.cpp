#include "project10/simulation/run.hpp"

#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/players.hpp"

#include <initializer_list>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;

namespace project10::simulation {

std::optional<GenerationResult> generateRun(int runID, const ConfirmOverwrite& confirm,
                                          const io::RunLock* borrowedLock) {
  const auto config = io::loadConfig(runID);
  // 生成を先に終え、生成失敗による空の結果ディレクトリを避ける。
  const auto players = generatePlayers(config.numPlayers);
  const auto parameters = generateLatentParameters(
    config.numPlayers, config.mean, config.stddev, config.abilitySeed
  );
  const auto matches = generateMatches(
    parameters, config.matchesPerPlayer, config.ratingPeriodMatchesPerPlayer,
    config.performanceStd, config.matchingSeed, config.resultSeed,
    config.sideAdvantage, config.conditionSeed, config.conditional
  );

  const auto outputDir = fs::path("runs") / io::runDirectoryName(runID);
  fs::create_directories("runs");
  const auto lockPath = fs::path("runs") / ("." + outputDir.filename().string() + ".lock");
  if (borrowedLock) borrowedLock->requirePath(lockPath);
  const auto lock = borrowedLock ? nullptr : std::make_unique<io::RunLock>(lockPath);
  if (!fs::create_directory(outputDir)) {
    if (fs::is_symlink(outputDir) || !fs::is_directory(outputDir)) {
      throw std::runtime_error("Output path must be a regular directory.");
    }
    if (config.conditional || fs::exists(fs::symlink_status(outputDir / "conditions.csv")))
      throw std::runtime_error("比較用runは上書きできません。新しいrun IDを使用してください。");
    if (!confirm || !confirm(outputDir)) return std::nullopt;
  }

  // 最後に保存する設定も含め、全出力先を先に検査する。
  std::vector<std::string> outputs{std::string("config.json"), config.playersFile,
                          config.latentParametersFile, config.groundTruthFile,
                          config.matchesFile};
  if (config.conditional) outputs.insert(outputs.end(), {"conditions.csv",
    "ground_truth_positive.csv", "ground_truth_negative.csv"});
  for (const auto& name : outputs) {
    const auto path = outputDir / name;
    const auto status = fs::symlink_status(path);
    if (fs::exists(status) && !fs::is_regular_file(status)) {
      throw std::runtime_error("Output path must be a regular file: " + path.string());
    }
    if (fs::is_regular_file(status) && fs::hard_link_count(path) > 1) {
      throw std::runtime_error("Output path must not have multiple hard links: " + path.string());
    }
  }

  writePlayers(players, (outputDir / config.playersFile).string());
  writeLatentParameters(parameters, (outputDir / config.latentParametersFile).string());
  writeGroundTruth(parameters, config.performanceStd, (outputDir / config.groundTruthFile).string(), config.sideAdvantage);
  writeMatches(matches, (outputDir / config.matchesFile).string());
  if (config.conditional) {
    io::writeConditions(matches, (outputDir / "conditions.csv").string());
    writeGroundTruth(parameters, config.performanceStd, (outputDir / "ground_truth_positive.csv").string(), config.sideAdvantage, 1);
    writeGroundTruth(parameters, config.performanceStd, (outputDir / "ground_truth_negative.csv").string(), config.sideAdvantage, -1);
  }
  io::writeConfigSnapshot(config, outputDir);
  return GenerationResult{config.numPlayers, outputDir};
}

}  // namespace project10::simulation
