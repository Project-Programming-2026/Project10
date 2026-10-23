// Glicko（初代）で1試行分の予測CSV（glicko.csv）を出力する。
// 入力は保存済みの設定・players.csv・matches.csvのみ。潜在能力と真の勝率は読まない。

#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/glicko.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

int main() {
  try {
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
    if (fs::exists(fs::symlink_status(output))) {
      throw std::runtime_error("Glicko出力先がすでに存在します: " + output.string());
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), rating.numPlayers);
    const auto matches = project10::io::readMatchesCsv(
      (dir / "matches.csv").string(), rating.numPlayers, rating.matchesPerPlayer, rating.period
    );

    // ロック領域で書き終えてから、既存ファイルを置き換えないハードリンクで公開する。
    const auto temporary = lockPath / rating.predictionsFile;
    try {
      std::ofstream file(temporary);
      if (!file) throw std::runtime_error("Glickoの一時ファイルを開けません。");
      writeGlickoPredictions(
        file, runID, rating.numPlayers, rating.interval, matches,
        config.initialRating, config.initialRD, config.rdGrowth
      );
      file.close();
      if (!file) throw std::runtime_error("Glicko予測CSVを保存できません。");
      fs::create_hard_link(temporary, output);
    } catch (...) {
      std::error_code ignored;
      fs::remove(temporary, ignored);
      throw;
    }
    std::error_code error;
    fs::remove(temporary, error);
    if (error) std::cerr << "一時ファイルを削除できません: " << temporary.string() << '\n';
    std::cout << "Glicko output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
