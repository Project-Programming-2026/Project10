// Glicko（初代）で1試行分の予測CSV（glicko.csv）を出力する。
// --final-ratingsを付けると、最後の期間の更新後のレート（glicko_rating.csv）も出力する。
// 入力は保存済みの設定・players.csv・matches.csvのみ。潜在能力と真の勝率は読まない。

#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/glicko.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ranked_glickoの基準に使う最終レート。ranked_glicko2のglicko2_rating.csvと同じ形式。
const std::string ratingFileName = "glicko_rating.csv";

// 1行目は「rating」、2行目以降はプレイヤーID順に、最後の期間の更新後のレートを1つずつ書く。
void writeFinalRatings(const fs::path& path, const std::vector<GlickoState>& states) {
  std::ofstream file(path);
  file.imbue(std::locale::classic());
  file << "rating\n" << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const GlickoState& state : states) {
    file << state.rating << '\n';
  }
  file.close();
  if (!file) throw std::runtime_error("Glickoの最終レートを保存できません。");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    // --final-ratingsを付けたときだけ、ranked_glickoの基準用に最終レートも書く。
    // 一括実行は引数なしで呼ぶので、他の推定器の作業先に余分なファイルを残さない。
    const bool writeRatings = argc == 2 && std::string(argv[1]) == "--final-ratings";
    if (argc > 2 || (argc == 2 && !writeRatings)) {
      throw std::invalid_argument("使い方: project10_glicko [--final-ratings]");
    }
    int runID;
    std::string input;
    std::cout << "Enter run ID: ";
    std::getline(std::cin, input);
    std::istringstream parser(input);
    if (!(parser >> runID) || runID < 0 || !(parser >> std::ws).eof()) {
      throw std::invalid_argument("run IDが不正です。0以上の整数を入力してください。");
    }
    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir)) {
      throw std::runtime_error("実行結果の保存先が通常のディレクトリではありません。");
    }
    // 生成・評価と同じロックで、読み込み中の入力の再生成を防ぐ。
    const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
    const project10::io::RunLock lock(lockPath);
    const auto config = project10::io::loadGlickoConfig(runID);
    const auto& rating = config.rating;
    const auto output = dir / rating.predictionsFile;
    const auto ratingOutput = dir / ratingFileName;
    std::vector<fs::path> outputs{output};
    if (writeRatings) outputs.push_back(ratingOutput);
    for (const auto& path : outputs) {
      if (fs::exists(fs::symlink_status(path))) {
        throw std::runtime_error("Glicko出力先がすでに存在します: " + path.string());
      }
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), rating.numPlayers);
    const auto matches = project10::io::readMatchesCsv(
      (dir / "matches.csv").string(), rating.numPlayers, rating.matchesPerPlayer, rating.period
    );

    // ロック領域で書き終えてから、既存ファイルを置き換えないハードリンクで公開する。
    const auto temporary = lockPath / rating.predictionsFile;
    const auto temporaryRatings = lockPath / ratingFileName;
    bool publishedPredictions = false;
    try {
      std::ofstream file(temporary);
      if (!file) throw std::runtime_error("Glickoの一時ファイルを開けません。");
      const auto finalStates = writeGlickoPredictions(
        file, runID, rating.numPlayers, rating.interval, matches,
        config.initialRating, config.initialRD, config.rdGrowth
      );
      file.close();
      if (!file) throw std::runtime_error("Glicko予測CSVを保存できません。");
      if (writeRatings) writeFinalRatings(temporaryRatings, finalStates);
      fs::create_hard_link(temporary, output);
      publishedPredictions = true;
      if (writeRatings) fs::create_hard_link(temporaryRatings, ratingOutput);
    } catch (...) {
      // 片方だけが公開された状態を残さない。
      std::error_code ignored;
      if (publishedPredictions) fs::remove(output, ignored);
      fs::remove(temporary, ignored);
      fs::remove(temporaryRatings, ignored);
      throw;
    }
    for (const auto& path : {temporary, temporaryRatings}) {
      std::error_code error;
      fs::remove(path, error);
      if (error) std::cerr << "一時ファイルを削除できません: " << path.string() << '\n';
    }
    std::cout << "Glicko output: " << output.string() << '\n';
    if (writeRatings) std::cout << "Glicko final ratings: " << ratingOutput.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
