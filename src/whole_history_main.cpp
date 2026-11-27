// 全履歴推定で1試行分の予測CSVを出力する。
// 使い方：project10_whole_history run_ID 事前分布のRD 出力ファイル名
// 出力はruns/run_XXXX/直下で、既存のファイルは上書きしない。MSEは
// project10_evaluateの--inputモードで共通の評価に通す。潜在能力と真の勝率は読まない。

#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/ratings/whole_history.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <regex>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

int parseRunID(const std::string& text) {
  if (!std::regex_match(text, std::regex("[0-9]{1,9}"))) {
    throw std::invalid_argument("run IDは0以上の整数で指定してください。");
  }
  return std::stoi(text);
}

double parsePriorRatingDeviation(const std::string& text) {
  std::size_t used = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &used);
  } catch (const std::exception&) {
    used = 0;
  }
  if (used != text.size() || !std::isfinite(value) || value <= 0.0) {
    throw std::invalid_argument("事前分布のRDは正の有限の数値で指定してください。");
  }
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      throw std::invalid_argument("使い方: project10_whole_history run_ID 事前分布のRD 出力ファイル名");
    }
    const int runID = parseRunID(argv[1]);
    const double priorRatingDeviation = parsePriorRatingDeviation(argv[2]);
    const std::string outputName = argv[3];
    // run直下のファイル名だけを受け付け、パスや..は拒否する。
    if (!std::regex_match(outputName, std::regex("[A-Za-z0-9_][A-Za-z0-9_.-]*\\.csv")) ||
        outputName.find("..") != std::string::npos) {
      throw std::invalid_argument("出力ファイル名には英数字・_・-・.からなる.csvの名前を指定してください。");
    }

    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir)) {
      throw std::runtime_error("実行結果の保存先が通常のディレクトリではありません。");
    }
    // 生成・評価と同じロックで、読み込み中の入力の再生成を防ぐ。
    const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
    const project10::io::RunLock lock(lockPath);
    const auto config = project10::io::loadSimulationConfigFile(dir / "config.json");
    const auto evaluation = project10::io::loadEvaluationContext(runID);
    if (config.runID != runID || config.conditional) {
      throw std::runtime_error("run IDが一致しないか、先手条件付きのrunです。全履歴推定は通常のrunだけに対応します。");
    }
    const auto output = dir / outputName;
    if (fs::exists(fs::symlink_status(output))) {
      throw std::runtime_error("出力先がすでに存在します: " + output.string());
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
    const auto matches = project10::io::readMatchesCsv((dir / "matches.csv").string(),
      config.numPlayers, config.matchesPerPlayer, config.ratingPeriodMatchesPerPlayer);

    // ロック領域で書き終えてから、既存ファイルを置き換えないハードリンクで公開する。
    const auto temporary = lockPath / outputName;
    try {
      std::ofstream file(temporary);
      if (!file) throw std::runtime_error("一時ファイルを開けません。");
      file.imbue(std::locale::classic());
      file << "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n"
           << std::setprecision(std::numeric_limits<double>::max_digits10);
      project10::ratings::predictWholeHistoryAtCheckpoints(config.numPlayers, evaluation.interval,
        matches, priorRatingDeviation, [&file, runID](int matchesPerPlayer, int playerA, int playerB,
                                                      double probability) {
          file << runID << ',' << matchesPerPlayer << ',' << playerA << ',' << playerB << ','
               << probability << '\n';
        });
      file.close();
      if (!file) throw std::runtime_error("予測CSVを保存できません。");
      fs::create_hard_link(temporary, output);
    } catch (...) {
      std::error_code ignored;
      fs::remove(temporary, ignored);
      throw;
    }
    std::error_code error;
    fs::remove(temporary, error);
    if (error) std::cerr << "一時ファイルを削除できません: " << temporary.string() << '\n';
    std::cout << "Whole-history output: " << output.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
