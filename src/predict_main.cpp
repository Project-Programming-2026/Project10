#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/rating_cli.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/glicko2.hpp"
#include "project10/ratings/ranked_glicko2.hpp"
#include "project10/simulation/players.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

// Elo/Glicko-2の既存関数への入口。Glicko-2は1プロセスで1回だけ呼ぶ。
int main(int argc, char** argv) {
  try {
    const auto options = project10::io::parseRatingCliOptions(argc, argv, true);
    if (options.help) {
      std::cout << "project10_predict runID Elo|Glicko2|AdvantageElo|rankedGlicko2 [--matches path] [--output path]\n"
                << "  Glicko2専用: --rating-output path / 比較用AdvantageElo専用: --conditions path, --online-output path\n";
      return 0;
    }
    const int runID = *options.runID;
    const std::string& algorithm = options.algorithm;
    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir))
      throw std::runtime_error("runの保存先には、シンボリックリンクではない既存のディレクトリが必要です。");
    const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
    const project10::io::RunLock lock(lockPath);
    const auto config = project10::io::loadRatingConfig(
      runID, algorithm, "runs", options.matchesPath.has_value(), options.outputPath.has_value());
    if (!config.conditional && (options.conditionsPath || options.onlineOutputPath))
      throw std::invalid_argument("--conditions と --online-output は比較用runの AdvantageElo でのみ指定できます。");
    const auto matchesPath = project10::io::resolveInputCsvPath(
      dir, options.matchesPath.value_or("matches.csv"), "対戦履歴CSV");
    const auto output = project10::io::resolveOutputCsvPath(
      dir, options.outputPath.value_or(config.predictionsFile), "予測出力先");
    const bool writeRating = algorithm == "Glicko2" && (options.ratingOutputPath || !options.outputPath);
    std::optional<fs::path> ratingOutput, conditionsPath, onlineOutput;
    if (writeRating) {
      ratingOutput = project10::io::resolveOutputCsvPath(
        dir, options.ratingOutputPath.value_or("glicko2_rating.csv"), "Glicko2の最終レート出力先");
      if (project10::io::areSameOutputPath(output, *ratingOutput))
        throw std::invalid_argument("予測出力先と最終レート出力先に同じパスは指定できません。");
    }
    const bool writeOnline = algorithm == "AdvantageElo" && config.conditional &&
      (options.onlineOutputPath || !options.outputPath);
    if (algorithm == "AdvantageElo" && config.conditional) {
      conditionsPath = project10::io::resolveInputCsvPath(
        dir, options.conditionsPath.value_or("conditions.csv"), "条件CSV");
      if (writeOnline) {
        onlineOutput = project10::io::resolveOutputCsvPath(
          dir, options.onlineOutputPath.value_or("online_predictions.csv"), "試合前予測ログ");
        if (project10::io::areSameOutputPath(output, *onlineOutput))
          throw std::invalid_argument("予測出力先と試合前予測ログに同じパスは指定できません。");
      }
    }
    if (algorithm == "rankedGlicko2") {
      const fs::path baseline("samples/glicko_baseline");
      if (fs::is_symlink("samples") || fs::is_symlink(baseline) ||
          !fs::is_regular_file(fs::symlink_status(baseline / "glicko2_rating.csv")))
        throw std::runtime_error("rankedGlicko2には通常ファイルのsamples/glicko_baseline/glicko2_rating.csvが必要です。");
      if (fs::exists(baseline / "config.json")) {
        if (!fs::is_regular_file(fs::symlink_status(baseline / "config.json")))
          throw std::runtime_error("固定参照の設定には、シンボリックリンクではない通常のファイルを指定してください。");
        const auto source = project10::io::loadSimulationConfigFile(baseline / "config.json");
        project10::io::readGlicko2RatingsCsv((baseline / "glicko2_rating.csv").string(), source.numPlayers);
      } else project10::io::readGlicko2RatingsCsv((baseline / "glicko2_rating.csv").string(), 0);
    }
    if ((algorithm == "Glicko2" || algorithm == "rankedGlicko2") &&
        (config.period != 10 || config.interval != 10 || config.matchesPerPlayer % 10 != 0))
      throw std::runtime_error("Glicko2とrankedGlicko2では、更新期間と評価間隔を1人当たり10試合、1人当たりの総対戦数を10の倍数に設定してください。");
    project10::io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
    auto matches = project10::io::readMatchesCsv(
      matchesPath.string(), config.numPlayers, config.matchesPerPlayer, config.period);
    if (algorithm == "Elo") {
      project10::io::writeEloPredictions(runID, generatePlayers(config.numPlayers), matches,
                                        config.interval, output.string());
    } else if (algorithm == "AdvantageElo") {
      std::unique_ptr<project10::io::StagedOutputFile> onlineStage;
      std::ofstream online;
      if (config.conditional) {
        project10::io::readConditionsCsv(conditionsPath->string(), matches);
        if (writeOnline) {
          onlineStage = std::make_unique<project10::io::StagedOutputFile>(*onlineOutput, dir, lockPath, "試合前予測ログ");
          online.open(onlineStage->tempPath());
          if (!online) throw std::runtime_error("試合前予測ログを開けません。");
        }
      }
      project10::io::writeAdvantageEloPredictions(runID, generatePlayers(config.numPlayers),
        matches, config.interval, output.string(), project10::io::loadAdvantageParameters(runID),
        config.conditional, writeOnline ? &online : nullptr);
      if (writeOnline) {
        try {
          online.close();
          if (!online) throw std::runtime_error("試合前予測ログを保存できません。");
          onlineStage->publish();
        } catch (...) {
          std::error_code ignored; fs::remove(output, ignored); throw;
        }
      }
    } else {
      project10::io::StagedOutputFile predStage(output, dir, lockPath, "予測出力先");
      std::unique_ptr<project10::io::StagedOutputFile> ratingStage;
      if (writeRating)
        ratingStage = std::make_unique<project10::io::StagedOutputFile>(*ratingOutput, dir, lockPath, "Glicko2の最終レート出力先");
      if (algorithm == "Glicko2")
        generate_glicko2_prediction(runID, dir, matchesPath, predStage.tempPath(), ratingStage ? ratingStage->tempPath() : fs::path{});
      else
        generate_ranked_glicko2_prediction(runID, dir, matchesPath, predStage.tempPath());
      if (!fs::is_regular_file(predStage.tempPath()) || fs::file_size(predStage.tempPath()) == 0)
        throw std::runtime_error("予測CSVが生成されませんでした。");
      predStage.publish();
      if (ratingStage) {
        try { ratingStage->publish(); }
        catch (...) { std::error_code ignored; fs::remove(output, ignored); throw; }
      }
    }
    if (!fs::is_regular_file(output) || fs::file_size(output) == 0)
      throw std::runtime_error("予測CSVが生成されませんでした。");
    std::cout << algorithm << " output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
}
