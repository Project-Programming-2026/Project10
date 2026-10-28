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

// Elo/Glicko-2の既存関数への入口。Glicko-2は1プロセスで1回だけ呼ぶ。
int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::invalid_argument("project10_predict runID Elo|Glicko2|rankedGlicko2");
    int runID;
    const std::string_view input = argv[1];
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), runID);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || runID < 0)
      throw std::invalid_argument("run IDが不正です。");
    const std::string algorithm = argv[2];
    if (algorithm != "Elo" && algorithm != "Glicko2" && algorithm != "rankedGlicko2") throw std::invalid_argument("未対応の手法名です。");
    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir)) throw std::runtime_error("通常のrunが必要です。");
    const project10::io::RunLock lock(fs::path("runs") / ("." + dir.filename().string() + ".lock"));
    const auto config = project10::io::loadRatingConfig(runID, algorithm);
    const auto output = dir / config.predictionsFile;
    if (fs::exists(fs::symlink_status(output))) throw std::runtime_error("予測出力先がすでに存在します。");
    if ((algorithm == "Glicko2" || algorithm == "rankedGlicko2") && (config.period != 10 || config.interval != 10 ||
                                   config.matchesPerPlayer % 10 != 0)) {
      throw std::runtime_error("Glicko2、rankedGlicko2は期間・評価間隔10、総対戦数は10の倍数に限定しています。");
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
    const auto matches = project10::io::readMatchesCsv(
      (dir / "matches.csv").string(), config.numPlayers, config.matchesPerPlayer, config.period);
    if (algorithm == "Elo") {
      project10::io::writeEloPredictions(runID, generatePlayers(config.numPlayers), matches,
                                        config.interval, output.string());
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
