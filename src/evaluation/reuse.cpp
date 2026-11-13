#include "project10/evaluation/reuse.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/simulation/probit.hpp"
#include "project10/evaluation/reference.hpp"

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

ReusePlan inspectReusableRun(int runID, const io::RunLock& lock, bool allMethods,
                             bool comparison, int referenceRun) {
  const auto run = fs::path("runs") / io::runDirectoryName(runID);
  lock.requirePath(run.parent_path() / ("." + run.filename().string() + ".lock"));
  if (fs::is_symlink(run) || !fs::is_directory(run))
    throw std::runtime_error("再利用するrunは通常のディレクトリが必要です。");
  for (const auto* name : {"project10_run.json", "config.json", "players.csv", "matches.csv",
                           "ground_truth.csv", "latent_parameters.csv"}) {
    if (!regularFile(run / name)) throw std::runtime_error("再利用に必要な成果物がありません: " + std::string(name));
  }
  io::validateRunSnapshot(runID, allMethods, comparison, referenceRun);
  const auto config = io::loadConfig(runID, "runs");
  io::validatePlayersCsv((run / "players.csv").string(), config.numPlayers);
  auto matches = io::readMatchesCsv((run / "matches.csv").string(), config.numPlayers,
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
      if (std::abs(truth[a][b] - calculateConditionalWinProbability(latent[a], latent[b],
          config.performanceStd, config.sideAdvantage, 0)) > 1e-12)
        throw std::runtime_error("潜在能力と正解勝率が一致しません。");
    }
  }
  ReusePlan plan;
  if (comparison) {
    if (!allMethods && (regularFile(run / "ranked_glicko2.csv") || regularFile(run / "ranked_glicko2_mse.csv")))
      throw std::runtime_error("独立参照の指定がないrunにrankedGlicko2の成果物があります。");
    for (const auto* name : {"conditions.csv", "ground_truth_positive.csv", "ground_truth_negative.csv"})
      if (!regularFile(run / name)) throw std::runtime_error("比較用の条件・正解CSVがありません。");
    io::readConditionsCsv((run / "conditions.csv").string(), matches);
    for (const int z : {1, -1}) {
      const auto values = io::readGroundTruthCsv((run / (z == 1 ? "ground_truth_positive.csv" :
        "ground_truth_negative.csv")).string(), config.numPlayers);
      for (int a = 0; a < config.numPlayers; ++a) for (int b = a + 1; b < config.numPlayers; ++b)
        if (std::abs(values[a][b] - calculateConditionalWinProbability(latent[a], latent[b],
            config.performanceStd, config.sideAdvantage, z)) > 1e-12)
          throw std::runtime_error("条件付き正解勝率が潜在能力・有利幅と一致しません。");
    }
  }
  if (allMethods) validateFixedReference(runID);
  if (allMethods || comparison) {
    const bool reference = regularFile(run / "glicko2_rating.csv");
    const bool prediction = regularFile(run / "glicko2.csv");
    if (reference != prediction)
      throw std::runtime_error("Glicko2予測と最終レートCSVの両方が必要です。");
    if (reference) io::readGlicko2RatingsCsv((run / "glicko2_rating.csv").string(), config.numPlayers);
  }
  for (const auto& method : io::ratingMethods(allMethods || comparison, allMethods)) {
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
  if (comparison) {
    const bool log = regularFile(run / "online_predictions.csv");
    if (log != (plan.predictions.count("AdvantageElo") != 0))
      throw std::runtime_error("AdvantageElo予測と試合前ログの両方が必要です。");
    const bool metrics = regularFile(run / "online_metrics.csv");
    if (metrics && !log) throw std::runtime_error("試合前評価に対応するログがありません。");
    if (log) {
      const auto rating = io::loadRatingConfig(runID, "Elo");
      const auto calculated = calculateOnlineMetrics((run / "online_predictions.csv").string(),
        runID, matches, config.numPlayers, rating.interval);
      if (metrics) {
        const auto saved = io::readOnlineMetricsCsv((run / "online_metrics.csv").string(),
          runID, config.matchesPerPlayer, rating.interval);
        for (std::size_t i = 0; i < saved.size(); ++i) {
          const auto& a = saved[i]; const auto& b = calculated[i];
          if (std::abs(a.eloLogLoss - b.eloLogLoss) > 1e-12 ||
              std::abs(a.advantageLogLoss - b.advantageLogLoss) > 1e-12 ||
              std::abs(a.eloBrier - b.eloBrier) > 1e-12 ||
              std::abs(a.advantageBrier - b.advantageBrier) > 1e-12)
            throw std::runtime_error("試合前評価がログの再計算値と一致しません。");
        }
        plan.onlineMetrics = true;
      }
    }
  }
  return plan;
}
}  // namespace project10::evaluation
