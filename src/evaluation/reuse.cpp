#include "project10/evaluation/reuse.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/simulation/probit.hpp"

#include <cmath>
#include <stdexcept>

namespace project10::evaluation {
namespace fs = std::filesystem;
namespace {
bool regularFile(const fs::path& path) {
  const auto status = fs::symlink_status(path);
  if (!fs::exists(status)) return false;
  if (!fs::is_regular_file(status))
    throw std::runtime_error("再利用する成果物は通常ファイルが必要です: " + path.string());
  return true;
}
}  // namespace

ReusePlan inspectReusableRun(int runID, const io::RunLock& lock) {
  const auto run = fs::path("runs") / io::runDirectoryName(runID);
  lock.requirePath(run.parent_path() / ("." + run.filename().string() + ".lock"));
  if (fs::is_symlink(run) || !fs::is_directory(run))
    throw std::runtime_error("再利用するrunは通常のディレクトリが必要です。");
  for (const auto* name : {"project10_run.json", "config.json", "players.csv", "matches.csv",
                           "ground_truth.csv", "latent_parameters.csv"}) {
    if (!regularFile(run / name)) throw std::runtime_error("再利用に必要な成果物がありません: " + std::string(name));
  }
  io::validateRunSnapshot(runID);
  const auto config = io::loadConfig(runID, "runs");
  io::validatePlayersCsv((run / "players.csv").string(), config.numPlayers);
  const auto matches = io::readMatchesCsv((run / "matches.csv").string(), config.numPlayers,
    config.matchesPerPlayer, config.ratingPeriodMatchesPerPlayer);
  std::vector<int> lastRound(config.numPlayers, -1);
  for (std::size_t i = 0; i < matches.size(); ++i) {
    const int round = static_cast<int>(i / (config.numPlayers / 2));
    const auto& match = matches[i];
    if (lastRound[match.playerA] == round || lastRound[match.playerB] == round)
      throw std::runtime_error("同一ラウンドの対戦プレイヤーが重複しています。");
    lastRound[match.playerA] = lastRound[match.playerB] = round;
  }
  const auto latent = io::readLatentParametersCsv((run / "latent_parameters.csv").string(), config.numPlayers);
  const auto truth = io::readGroundTruthCsv((run / "ground_truth.csv").string(), config.numPlayers);
  for (int a = 0; a < config.numPlayers; ++a) {
    for (int b = a + 1; b < config.numPlayers; ++b) {
      if (std::abs(truth[a][b] - calculateWinProbability(latent[a], latent[b], config.performanceStd)) > 1e-12)
        throw std::runtime_error("潜在能力と正解勝率が一致しません。");
    }
  }
  ReusePlan plan;
  for (const auto* method : {"Elo", "Glicko", "Glicko2", "TrueSkill"}) {
    const auto evaluation = io::loadEvaluationConfig(runID, method);
    const bool prediction = regularFile(run / evaluation.predictionsFile);
    const bool mse = regularFile(run / evaluation.mseFile);
    if (mse && !prediction) throw std::runtime_error(std::string(method) + ": MSEに対応する予測CSVがありません。");
    if (!prediction) continue;
    const auto calculated = calculateRunMse(runID, method, &lock);
    plan.predictions.insert(method);
    if (!mse) continue;
    const auto cached = io::readMseCsv((run / evaluation.mseFile).string(), runID,
      evaluation.matchesPerPlayer, evaluation.interval);
    for (std::size_t i = 0; i < cached.size(); ++i) {
      if (std::abs(cached[i].mse - calculated[i].mse) > 1e-12 + 1e-10 * std::abs(calculated[i].mse))
        throw std::runtime_error(std::string(method) + ": 保存済みMSEが再計算値と一致しません。");
    }
    plan.mse.insert(method);
  }
  return plan;
}
}  // namespace project10::evaluation
