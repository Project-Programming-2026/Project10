#include "project10/evaluation/reference.hpp"
#include "project10/io/csv.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace project10::evaluation {
namespace fs = std::filesystem;
namespace {
void regular(const fs::path& path) {
  if (!fs::is_regular_file(fs::symlink_status(path)))
    throw std::runtime_error("参照分布には通常ファイルが必要です: " + path.string());
}
void directory(const fs::path& path) {
  if (fs::is_symlink(path) || !fs::is_directory(path))
    throw std::runtime_error("参照分布のディレクトリが不正です。");
}
std::string text(const fs::path& path) {
  regular(path);
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("参照分布を開けません: " + path.string());
  std::ostringstream result; result << file.rdbuf();
  if (file.bad()) throw std::runtime_error("参照分布を読み込めません。");
  return result.str();
}
void compatible(const io::SimulationConfig& target, const io::SimulationConfig& ref) {
  if (target.runID == ref.runID || target.abilitySeed == ref.abilitySeed ||
      target.matchingSeed == ref.matchingSeed || target.resultSeed == ref.resultSeed ||
      target.conditionSeed == ref.conditionSeed)
    throw std::runtime_error("参照分布は別run・別の全シードで生成してください。");
  if (!target.conditional || !ref.conditional || target.scenario != ref.scenario ||
      target.numPlayers != ref.numPlayers || target.mean != ref.mean || target.stddev != ref.stddev ||
      target.performanceStd != ref.performanceStd || target.sideAdvantage != ref.sideAdvantage ||
      target.ratingPeriodMatchesPerPlayer != ref.ratingPeriodMatchesPerPlayer)
    throw std::runtime_error("参照分布と評価runの生成条件が一致しません。");
}
void independent(const io::SimulationConfig& target, const io::SimulationConfig& ref) {
  if (target.runID == ref.runID || target.abilitySeed == ref.abilitySeed ||
      target.matchingSeed == ref.matchingSeed || target.resultSeed == ref.resultSeed)
    throw std::runtime_error("固定参照とは別run・別の能力／組合せ／勝敗シードを使用してください。");
}
ReferenceData fixedData(const fs::path& root, const io::SimulationConfig& target) {
  directory(root);
  regular(root / "config.json"); regular(root / "glicko2_rating.csv");
  const auto reference = io::loadSimulationConfigFile(root / "config.json");
  independent(target, reference);
  // 分布の学習人数は評価人数と独立。参照側の設定人数で完全性を検査する。
  io::readGlicko2RatingsCsv((root / "glicko2_rating.csv").string(), reference.numPlayers);
  return {reference.sourceText, text(root / "glicko2_rating.csv")};
}
}
fs::path fixedReferenceDirectory(int runID) {
  return fs::path("runs") / io::runDirectoryName(runID) / "ranked_reference/glicko_baseline";
}
ReferenceData captureFixedReference(const io::SimulationConfig& target) {
  directory("samples");
  return fixedData("samples/glicko_baseline", target);
}
void validateFixedReference(int runID) {
  if (!io::isFixedReferenceRun(runID)) throw std::runtime_error("固定参照の実行モードを確認できません。");
  const auto saved = fixedReferenceDirectory(runID);
  directory(saved.parent_path());
  fixedData(saved, io::loadConfig(runID, "runs"));
}
fs::path referenceDirectory(int targetRun, int referenceRun) {
  return fs::path("runs") / io::runDirectoryName(targetRun) / "ranked_reference" /
    io::runDirectoryName(referenceRun);
}
void validateSavedReference(const io::SimulationConfig& target, int referenceRun) {
  const auto root = referenceDirectory(target.runID, referenceRun);
  directory(root.parent_path()); directory(root);
  regular(root / "config.json"); regular(root / "glicko2_rating.csv");
  const auto config = io::loadConfig(referenceRun, root.parent_path());
  compatible(target, config);
  io::readGlicko2RatingsCsv((root / "glicko2_rating.csv").string(), config.numPlayers);
}
}  // namespace project10::evaluation
