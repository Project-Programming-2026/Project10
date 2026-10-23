#include "project10/evaluation/run.hpp"

#include "project10/evaluation/mse.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"

#include <stdexcept>
#include <memory>
#include <system_error>

namespace fs = std::filesystem;

namespace project10::evaluation {

std::vector<MseResult> calculateRunMse(int runID, const std::string& algorithm,
                                      const io::RunLock* borrowedLock) {
  const auto dir = fs::path("runs") / io::runDirectoryName(runID);
  if (fs::is_symlink(dir) || !fs::is_directory(dir))
    throw std::runtime_error("実行結果の保存先が通常のディレクトリではありません。");
  const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
  if (borrowedLock) borrowedLock->requirePath(lockPath);
  const auto lock = borrowedLock ? nullptr : std::make_unique<io::RunLock>(lockPath);
  const auto config = io::loadEvaluationConfig(runID, algorithm);
  io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
  MseEvaluator evaluator(
    runID, config.matchesPerPlayer, config.interval,
    io::readGroundTruthCsv((dir / "ground_truth.csv").string(), config.numPlayers)
  );
  io::readPredictionsCsv((dir / config.predictionsFile).string(), evaluator);
  return evaluator.finish();
}

EvaluationResult evaluateRun(int runID, const std::string& algorithm,
                             const io::RunLock* borrowedLock) {
  const auto dir = fs::path("runs") / io::runDirectoryName(runID);
  if (fs::is_symlink(dir) || !fs::is_directory(dir)) {
    throw std::runtime_error("実行結果の保存先が存在しないか、通常のディレクトリではありません。");
  }
  const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
  if (borrowedLock) borrowedLock->requirePath(lockPath);
  const auto lock = borrowedLock ? nullptr : std::make_unique<io::RunLock>(lockPath);
  const auto config = io::loadEvaluationConfig(runID, algorithm);
  const auto output = dir / config.mseFile;
  if (fs::exists(fs::symlink_status(output))) {
    throw std::runtime_error("MSE出力先がすでに存在します: " + output.string());
  }
  const auto results = calculateRunMse(runID, algorithm, borrowedLock ? borrowedLock : lock.get());
  // ロックの専用領域で保存後、既存出力を置き換えないハードリンクで公開する。
  const auto temporary = lockPath / config.mseFile;
  try {
    io::writeMseCsv(temporary.string(), runID, results);
    io::publishMseCsv(temporary.string(), output.string());
  } catch (...) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
    throw;
  }
  std::error_code cleanupError;
  fs::remove(temporary, cleanupError);
  return {output, cleanupError ? temporary : fs::path{}};
}

}  // namespace project10::evaluation
