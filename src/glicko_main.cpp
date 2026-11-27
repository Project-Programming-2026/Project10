// Glicko（初代）で1試行分の予測CSV（既定: glicko.csv）を出力する。
// 入力は保存済みの設定・players.csv・対戦履歴CSVのみ。潜在能力と真の勝率は読まない。

#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/rating_cli.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/glicko.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  try {
    const auto options = project10::io::parseRatingCliOptions(argc, argv, false);
    if (options.help) {
      std::cout << "project10_glicko [runID] [--matches <path>] [--output <path>]\n"
                << "  runIDを省略した場合は標準入力から読み取ります。\n";
      return 0;
    }
    const int runID = project10::io::resolveOrPromptRunID(options);
    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir))
      throw std::runtime_error("実行結果の保存先が通常のディレクトリではありません。");
    // 生成・評価と同じロックで、読み込み中の入力の再生成を防ぐ。
    const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
    const project10::io::RunLock lock(lockPath);
    const auto config = project10::io::loadGlickoConfig(
      runID, "runs", options.matchesPath.has_value(), options.outputPath.has_value());
    const auto& rating = config.rating;
    const auto matchesPath = project10::io::resolveInputCsvPath(
      dir, options.matchesPath.value_or("matches.csv"), "対戦履歴CSV");
    const auto output = project10::io::resolveOutputCsvPath(
      dir, options.outputPath.value_or(rating.predictionsFile), "Glicko出力先");
    project10::io::validatePlayersCsv((dir / "players.csv").string(), rating.numPlayers);
    const auto matches = project10::io::readMatchesCsv(
      matchesPath.string(), rating.numPlayers, rating.matchesPerPlayer, rating.period);

    project10::io::StagedOutputFile staged(output, dir, lockPath, "Glicko出力先");
    std::ofstream file(staged.tempPath());
    if (!file) throw std::runtime_error("Glickoの一時ファイルを開けません。");
    writeGlickoPredictions(file, runID, rating.numPlayers, rating.interval, matches,
      config.initialRating, config.initialRD, config.rdGrowth);
    file.close();
    if (!file) throw std::runtime_error("Glicko予測CSVを保存できません。");
    staged.publish();
    std::cout << "Glicko output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
