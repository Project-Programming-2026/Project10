#include "project10/evaluation/run.hpp"

#include "project10/evaluation/mse.hpp"
#include "project10/evaluation/ranking.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"

#include <stdexcept>
#include <memory>
#include <system_error>

namespace fs = std::filesystem;

namespace project10::evaluation {

std::vector<MseResult> calculateFileMse(const io::EvaluationContext& config,
                                      const fs::path& predictions, bool conditionalPredictions) {
  const int runID = config.runID;
  const auto dir = fs::path("runs") / io::runDirectoryName(runID);
  if (conditionalPredictions && !config.conditional)
    throw std::invalid_argument("通常MSEには条件z付き予測CSVを使用できません。");
  io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
  if (config.conditional) {
    MseEvaluator positive(runID, config.matchesPerPlayer, config.interval,
      io::readGroundTruthCsv((dir / "ground_truth_positive.csv").string(), config.numPlayers));
    MseEvaluator negative(runID, config.matchesPerPlayer, config.interval,
      io::readGroundTruthCsv((dir / "ground_truth_negative.csv").string(), config.numPlayers));
    const auto path = predictions.string();
    if (conditionalPredictions) io::readConditionalPredictionsCsv(path, positive, negative);
    else io::consumePredictionsCsv(path, [&](const auto& row) {
      positive.addPrediction(row); negative.addPrediction(row);
    });
    auto results = positive.finish();
    const auto other = negative.finish();
    for (std::size_t i = 0; i < results.size(); ++i)
      results[i].mse = 0.5 * (results[i].mse + other[i].mse);
    return results;
  }
  MseEvaluator evaluator(
    runID, config.matchesPerPlayer, config.interval,
    io::readGroundTruthCsv((dir / "ground_truth.csv").string(), config.numPlayers)
  );
  io::readPredictionsCsv(predictions.string(), evaluator);
  return evaluator.finish();
}

std::vector<MseResult> calculateRunMse(int runID, const std::string& algorithm,
                                      const io::RunLock* borrowedLock) {
  const auto dir = fs::path("runs") / io::runDirectoryName(runID);
  if (fs::is_symlink(dir) || !fs::is_directory(dir))
    throw std::runtime_error("実行結果の保存先が通常のディレクトリではありません。");
  const auto lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
  if (borrowedLock) borrowedLock->requirePath(lockPath);
  const auto lock = borrowedLock ? nullptr : std::make_unique<io::RunLock>(lockPath);
  const auto config = io::loadEvaluationConfig(runID, algorithm);
  return calculateFileMse({runID, config.numPlayers, config.matchesPerPlayer, config.interval,
    config.conditional, {}, {}}, dir / config.predictionsFile,
    config.conditional && algorithm == "AdvantageElo");
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

namespace {
fs::path lockRunDirectory(int runID, const io::RunLock* borrowed, std::unique_ptr<io::RunLock>& owned, fs::path& lockPath) {
  const auto dir = fs::path("runs") / io::runDirectoryName(runID);
  if (fs::is_symlink(dir) || !fs::is_directory(dir))
    throw std::runtime_error("実行結果の保存先が存在しないか、通常のディレクトリではありません。");
  lockPath = fs::path("runs") / ("." + dir.filename().string() + ".lock");
  if (borrowed) borrowed->requirePath(lockPath);
  else owned = std::make_unique<io::RunLock>(lockPath);
  return dir;
}
}  // namespace

std::vector<RankingResult> calculateFileRanking(const io::EvaluationContext& config,
                                                const fs::path& predictions, bool conditionalPredictions) {
  if (conditionalPredictions && !config.conditional)
    throw std::invalid_argument("通常評価には条件z付き予測CSVを使用できません。");
  const auto dir = fs::path("runs") / io::runDirectoryName(config.runID);
  io::validatePlayersCsv((dir / "players.csv").string(), config.numPlayers);
  RankingEvaluator evaluator(config.runID, config.matchesPerPlayer, config.interval,
    io::readLatentParametersCsv((dir / "latent_parameters.csv").string(), config.numPlayers), conditionalPredictions);
  if (conditionalPredictions) io::readConditionalPredictionsCsv(predictions.string(), evaluator);
  else io::readPredictionsCsv(predictions.string(), evaluator);
  return evaluator.finish();
}

std::vector<RankingResult> calculateRunRanking(int runID, const std::string& algorithm, const io::RunLock* borrowedLock) {
  std::unique_ptr<io::RunLock> lock; fs::path lockPath;
  const auto dir = lockRunDirectory(runID, borrowedLock, lock, lockPath);
  const auto cfg = io::loadEvaluationConfig(runID, algorithm);
  return calculateFileRanking({runID, cfg.numPlayers, cfg.matchesPerPlayer, cfg.interval, cfg.conditional, {}, {}},
    dir / cfg.predictionsFile, cfg.conditional && algorithm == "AdvantageElo");
}

EvaluationResult evaluateRunRanking(int runID, const std::string& algorithm, const io::RunLock* borrowedLock) {
  std::unique_ptr<io::RunLock> lock; fs::path lockPath;
  const auto dir = lockRunDirectory(runID, borrowedLock, lock, lockPath);
  const auto cfg = io::loadEvaluationConfig(runID, algorithm);
  const auto output = dir / cfg.rankingFile;
  if (fs::exists(fs::symlink_status(output))) throw std::runtime_error("順位評価出力先がすでに存在します: " + output.string());
  const auto results = calculateFileRanking({runID, cfg.numPlayers, cfg.matchesPerPlayer, cfg.interval, cfg.conditional, {}, {}},
    dir / cfg.predictionsFile, cfg.conditional && algorithm == "AdvantageElo");
  const auto tmp = lockPath / cfg.rankingFile;
  try { io::writeRankingCsv(tmp.string(), runID, results); io::publishMseCsv(tmp.string(), output.string()); }
  catch (...) { std::error_code ignored; fs::remove(tmp, ignored); throw; }
  std::error_code cleanupError; fs::remove(tmp, cleanupError);
  return {output, cleanupError ? tmp : fs::path{}};
}

}  // namespace project10::evaluation
