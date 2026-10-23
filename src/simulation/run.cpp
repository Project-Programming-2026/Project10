#include "project10/simulation/run.hpp"

#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/players.hpp"

#include <initializer_list>
#include <stdexcept>

namespace fs = std::filesystem;

namespace project10::simulation {

std::optional<GenerationResult> generateRun(int runID, const ConfirmOverwrite& confirm) {
  const auto config = io::loadConfig(runID);
  // 生成を先に終え、生成失敗による空の結果ディレクトリを避ける。
  const auto players = generatePlayers(config.numPlayers);
  const auto parameters = generateLatentParameters(
    config.numPlayers, config.mean, config.stddev, config.abilitySeed
  );
  const auto matches = generateMatches(
    parameters, config.matchesPerPlayer, config.ratingPeriodMatchesPerPlayer,
    config.performanceStd, config.matchingSeed, config.resultSeed
  );

  const auto outputDir = fs::path("runs") / io::runDirectoryName(runID);
  fs::create_directories("runs");
  const io::RunLock lock(fs::path("runs") / ("." + outputDir.filename().string() + ".lock"));
  if (!fs::create_directory(outputDir)) {
    if (fs::is_symlink(outputDir) || !fs::is_directory(outputDir)) {
      throw std::runtime_error("Output path must be a regular directory.");
    }
    if (!confirm || !confirm(outputDir)) return std::nullopt;
  }

  // 最後に保存する設定も含め、全出力先を先に検査する。
  for (const auto& name : {std::string("config.json"), config.playersFile,
                          config.latentParametersFile, config.groundTruthFile,
                          config.matchesFile}) {
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
  writeGroundTruth(parameters, config.performanceStd, (outputDir / config.groundTruthFile).string());
  writeMatches(matches, (outputDir / config.matchesFile).string());
  io::writeConfigSnapshot(config, outputDir);
  return GenerationResult{config.numPlayers, outputDir};
}

}  // namespace project10::simulation
