#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/glicko2.hpp"
#include "project10/ratings/ranked_glicko2.hpp"
#include "project10/simulation/players.hpp"

#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <fstream>

// Elo/Glicko-2の既存関数への入口。Glicko-2は1プロセスで1回だけ呼ぶ。
int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::invalid_argument("project10_predict runID Elo|Glicko2|AdvantageElo|rankedGlicko2");
    int runID;
    const std::string_view input = argv[1];
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), runID);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || runID < 0)
      throw std::invalid_argument("run IDが不正です。");
    const std::string algorithm = argv[2];
    if (algorithm != "Elo" && algorithm != "Glicko2" && algorithm != "rankedGlicko2" &&
        algorithm != "AdvantageElo") throw std::invalid_argument("未対応の手法名です。");
    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir)) throw std::runtime_error("通常のrunが必要です。");
    const project10::io::RunLock lock(fs::path("runs") / ("." + dir.filename().string() + ".lock"));
    const auto config = project10::io::loadRatingConfig(runID, algorithm);
    const auto output = dir / config.predictionsFile;
    if (fs::exists(fs::symlink_status(output))) throw std::runtime_error("予測出力先がすでに存在します。");
    const auto reference = dir / "glicko2_rating.csv";
    if (algorithm == "Glicko2" && fs::exists(fs::symlink_status(reference)))
      throw std::runtime_error("Glicko2の最終レート出力先がすでに存在します。");
    if (algorithm == "rankedGlicko2") {
      const fs::path baseline("samples/glicko_baseline");
      if (fs::is_symlink("samples") || fs::is_symlink(baseline) ||
          !fs::is_regular_file(fs::symlink_status(baseline / "glicko2_rating.csv")))
        throw std::runtime_error("rankedGlicko2には通常ファイルのsamples/glicko_baseline/glicko2_rating.csvが必要です。");
      // 参照人数は評価人数と独立。固定分布の設定がある場合は、その人数で完全性を検査。
      // 隔離作業先では親が検証済みのCSVのみを渡す。
      if (fs::exists(baseline / "config.json")) {
        if (!fs::is_regular_file(fs::symlink_status(baseline / "config.json")))
          throw std::runtime_error("固定参照の設定は通常ファイルが必要です。");
        const auto source = project10::io::loadSimulationConfigFile(baseline / "config.json");
        project10::io::readGlicko2RatingsCsv((baseline / "glicko2_rating.csv").string(), source.numPlayers);
      } else project10::io::readGlicko2RatingsCsv((baseline / "glicko2_rating.csv").string(), 0);
    }
    if ((algorithm == "Glicko2" || algorithm == "rankedGlicko2") && (config.period != 10 || config.interval != 10 ||
                                   config.matchesPerPlayer % 10 != 0)) {
      throw std::runtime_error("Glicko2、rankedGlicko2は期間・評価間隔10、総対戦数は10の倍数に限定しています。");
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
    auto matches = project10::io::readMatchesCsv(
      (dir / "matches.csv").string(), config.numPlayers, config.matchesPerPlayer, config.period);
    if (algorithm == "Elo") {
      project10::io::writeEloPredictions(runID, generatePlayers(config.numPlayers), matches,
                                        config.interval, output.string());
    } else if (algorithm == "AdvantageElo") {
      std::ofstream online;
      if (config.conditional) {
        const auto conditions = dir / "conditions.csv";
        if (!fs::is_regular_file(fs::symlink_status(conditions)))
          throw std::runtime_error("通常ファイルのconditions.csvが必要です。");
        project10::io::readConditionsCsv(conditions.string(), matches);
        const auto log = dir / "online_predictions.csv";
        if (fs::exists(fs::symlink_status(log))) throw std::runtime_error("試合前予測ログがすでに存在します。");
        online.open(log);
        if (!online) throw std::runtime_error("試合前予測ログを開けません。");
      }
      project10::io::writeAdvantageEloPredictions(runID, generatePlayers(config.numPlayers),
        matches, config.interval, output.string(), project10::io::loadAdvantageParameters(runID),
        config.conditional, config.conditional ? &online : nullptr);
      if (config.conditional) {
        online.close();
        if (!online) throw std::runtime_error("試合前予測ログを保存できません。");
      }
    } else if(algorithm == "Glicko2"){
      generate_glicko2_prediction(runID, dir);
    } else{
      generate_ranked_glicko2_prediction(runID,dir);
    }
    if (!fs::is_regular_file(output) || fs::file_size(output) == 0)
      throw std::runtime_error("予測CSVが生成されませんでした。");
    std::cout << algorithm << " output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
}
