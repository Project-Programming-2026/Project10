// ranked_glicko（Glicko初代版）で1試行分の予測CSV（ranked_glicko.csv）を出力する。
// 入力は保存済みの設定・players.csv・matches.csvと、別シードの基準runの最終レートのみ。
// 潜在能力と真の勝率は読まない。初期値・RD増加はGlickoと同じ設定のglickoを使い、
// 分布倍率の定数はranked_glicko.hppにある。Project10ディレクトリから実行する。

#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/ranked_glicko.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// 共通の設定の手法一覧には登録せず、出力名はここで決める。
const std::string outputFileName = "ranked_glicko.csv";
// 基準の分布Q。ranked_glicko2（PR #28）と同じく、どのrunでも常にこのファイルを使う。
const fs::path referenceFile = "samples/ranked_glicko_baseline/glicko_rating.csv";

std::vector<double> loadReferenceRatios() {
  if (fs::is_symlink(referenceFile) || !fs::is_regular_file(referenceFile)) {
    throw std::runtime_error("基準の最終レートがありません（Project10ディレクトリから実行してください）: " +
                             referenceFile.string());
  }
  std::ifstream file(referenceFile);
  if (!file) throw std::runtime_error("基準の最終レートを開けません: " + referenceFile.string());
  try {
    return readReferenceRatios(file);
  } catch (const std::exception& error) {
    throw std::runtime_error("基準の最終レートが不正です: " + referenceFile.string() + ": " + error.what());
  }
}

}  // namespace

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
    const auto output = dir / outputFileName;
    if (fs::exists(fs::symlink_status(output))) {
      throw std::runtime_error("ranked_glicko出力先がすでに存在します: " + output.string());
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), rating.numPlayers);
    const auto matches = project10::io::readMatchesCsv(
      (dir / "matches.csv").string(), rating.numPlayers, rating.matchesPerPlayer, rating.period
    );
    const auto referenceRatios = loadReferenceRatios();
    const RankedGlickoParameters parameters{
      config.initialRating, config.initialRD, config.rdGrowth
    };

    // ロック領域で書き終えてから、既存ファイルを置き換えないハードリンクで公開する。
    const auto temporary = lockPath / outputFileName;
    try {
      std::ofstream file(temporary);
      if (!file) throw std::runtime_error("ranked_glickoの一時ファイルを開けません。");
      writeRankedGlickoPredictions(
        file, runID, rating.numPlayers, rating.interval, matches, referenceRatios, parameters
      );
      file.close();
      if (!file) throw std::runtime_error("ranked_glicko予測CSVを保存できません。");
      fs::create_hard_link(temporary, output);
    } catch (...) {
      std::error_code ignored;
      fs::remove(temporary, ignored);
      throw;
    }
    std::error_code error;
    fs::remove(temporary, error);
    if (error) std::cerr << "一時ファイルを削除できません: " << temporary.string() << '\n';
    std::cout << "Ranked Glicko output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
