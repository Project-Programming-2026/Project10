#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/trueskill.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
    const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
    const project10::io::RunLock lock(lockPath);
    const auto config = project10::io::loadTrueSkillConfig(runID);
    const auto output = dir / config.predictionsFile;
    if (fs::exists(fs::symlink_status(output))) {
      throw std::runtime_error("TrueSkill出力先がすでに存在します: " + output.string());
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
    const auto matches = project10::io::readMatchesCsv(
      (dir / "matches.csv").string(), config.numPlayers, config.matchesPerPlayer, config.period
    );
    const auto temporary = lockPath / config.predictionsFile;
    try {
      std::ofstream file(temporary);
      if (!file) throw std::runtime_error("TrueSkillの一時ファイルを開けません。");
      project10::ratings::writeTrueSkillPredictions(
        file, runID, config.numPlayers, config.interval, matches
      );
      file.close();
      if (!file) throw std::runtime_error("TrueSkill予測CSVを保存できません。");
      fs::create_hard_link(temporary, output);
    } catch (...) {
      std::error_code ignored;
      fs::remove(temporary, ignored);
      throw;
    }
    std::error_code error;
    fs::remove(temporary, error);
    if (error) std::cerr << "一時ファイルを削除できません: " << temporary.string() << '\n';
    std::cout << "TrueSkill output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
