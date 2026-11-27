#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/rating_cli.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/trueskill.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  try {
    const auto options = project10::io::parseRatingCliOptions(argc, argv, false);
    if (options.help) {
      std::cout << "project10_trueskill [runID] [--matches <path>] [--output <path>]\n"
                << "  runIDを省略した場合は標準入力から読み取ります。\n";
      return 0;
    }
    const int runID = project10::io::resolveOrPromptRunID(options);
    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir))
      throw std::runtime_error("実行結果の保存先が通常のディレクトリではありません。");
    const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
    const project10::io::RunLock lock(lockPath);
    const auto config = project10::io::loadTrueSkillConfig(
      runID, options.matchesPath.has_value(), options.outputPath.has_value());
    const auto matchesPath = project10::io::resolveInputCsvPath(
      dir, options.matchesPath.value_or("matches.csv"), "対戦履歴CSV");
    const auto output = project10::io::resolveOutputCsvPath(
      dir, options.outputPath.value_or(config.predictionsFile), "TrueSkill出力先");
    project10::io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
    const auto matches = project10::io::readMatchesCsv(
      matchesPath.string(), config.numPlayers, config.matchesPerPlayer, config.period);

    project10::io::StagedOutputFile staged(output, dir, lockPath, "TrueSkill出力先");
    std::ofstream file(staged.tempPath());
    if (!file) throw std::runtime_error("TrueSkillの一時ファイルを開けません。");
    project10::ratings::writeTrueSkillPredictions(
      file, runID, config.numPlayers, config.interval, matches);
    file.close();
    if (!file) throw std::runtime_error("TrueSkill予測CSVを保存できません。");
    staged.publish();
    std::cout << "TrueSkill output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
