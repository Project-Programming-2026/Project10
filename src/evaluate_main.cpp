#include "project10/evaluation/mse.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"

#include <filesystem>
#include <iostream>
#include <new>
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
    std::cout << "Enter algorithm (Elo/Glicko/Glicko2/TrueSkill): ";
    std::string algorithm;
    std::getline(std::cin, algorithm);
    const auto dir = fs::path("runs") / project10::io::runDirectoryName(runID);
    if (fs::is_symlink(dir) || !fs::is_directory(dir)) {
      throw std::runtime_error("実行結果の保存先が存在しないか、通常のディレクトリではありません。");
    }
    const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
    const project10::io::RunLock lock(lockPath);
    const auto config = project10::io::loadEvaluationConfig(runID, algorithm);
    const auto output = dir / config.mseFile;
    if (fs::exists(fs::symlink_status(output))) {
      throw std::runtime_error("MSE出力先がすでに存在します: " + output.string());
    }
    project10::io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
    project10::evaluation::MseEvaluator evaluator(
      runID, config.matchesPerPlayer, config.interval,
      project10::io::readGroundTruthCsv((dir / "ground_truth.csv").string(), config.numPlayers)
    );
    project10::io::readPredictionsCsv((dir / config.predictionsFile).string(), evaluator);
    const auto results = evaluator.finish();

    // ロックとして新規作成した専用領域で保存を完了してから公開する。
    // 検証・保存失敗時には自分が作った一時ファイルだけを取り除く。
    const auto temporary = lockPath / config.mseFile;
    try {
      project10::io::writeMseCsv(temporary.string(), runID, results);
      if (fs::exists(fs::symlink_status(output))) {
        throw std::runtime_error("MSE出力先がすでに存在します: " + output.string());
      }
      fs::rename(temporary, output);
    } catch (...) {
      std::error_code ignored;
      fs::remove(temporary, ignored);
      throw;
    }
    std::cout << "MSE output: " << output.string() << '\n';
  } catch (const fs::filesystem_error& error) {
    std::cerr << "エラー: ファイル操作に失敗しました（エラーコード: " << error.code().value()
              << "）: " << error.path1().string() << '\n';
    return 1;
  } catch (const std::bad_alloc&) {
    std::cerr << "エラー: MSE評価に必要なメモリを確保できません。\n";
    return 1;
  } catch (const std::length_error&) {
    std::cerr << "エラー: 評価データが大きすぎて処理できません。\n";
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
