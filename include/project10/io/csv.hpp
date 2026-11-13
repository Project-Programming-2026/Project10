#pragma once

#include "project10/simulation/matches.hpp"
#include "project10/evaluation/mse.hpp"
#include "project10/ratings/elo.hpp"
#include "project10/ratings/advantage_elo.hpp"
#include "project10/evaluation/online.hpp"

#include <string>
#include <vector>
#include <functional>
#include <iosfwd>

void writePlayers(const std::vector<int>& players, const std::string& outputPath);
void writeMatches(const std::vector<Match>& matches, const std::string& outputPath);
void writeLatentParameters(
  const std::vector<double>& parameters,
  const std::string& outputPath
);

void writeGroundTruth(
  const std::vector<double>& parameters,
  double performanceStd,
  const std::string& outputPath, double sideAdvantage = 0, int condition = 0
);

namespace project10::io {

void validatePlayersCsv(const std::string& path, int numPlayers);
std::vector<double> readLatentParametersCsv(const std::string& path, int numPlayers);
std::vector<Match> readMatchesCsv(
  const std::string& path, int numPlayers, int matchesPerPlayer, int period
);
evaluation::ProbabilityMatrix readGroundTruthCsv(const std::string& path, int numPlayers);
void readPredictionsCsv(const std::string& path, evaluation::MseEvaluator& evaluator);
void consumePredictionsCsv(const std::string& path,
  const std::function<void(const evaluation::PredictionRow&)>& consume);
void readConditionalPredictionsCsv(const std::string& path,
  evaluation::MseEvaluator& positive, evaluation::MseEvaluator& negative);
void writeConditions(const std::vector<Match>& matches, const std::string& path);
void readConditionsCsv(const std::string& path, std::vector<Match>& matches);
void readOnlinePredictionsCsv(const std::string& path,
  const std::function<void(const evaluation::OnlinePrediction&)>& consume);
std::vector<evaluation::OnlineMetrics> readOnlineMetricsCsv(const std::string& path,
  int runID, int matchesPerPlayer, int interval);
void writeOnlineMetricsCsv(const std::string& path, int runID,
  const std::vector<evaluation::OnlineMetrics>& metrics);
// numPlayers=0は人数不明の分布入力（最低2人）。通常は参照設定の人数で検査する。
std::vector<double> readGlicko2RatingsCsv(const std::string& path, int numPlayers);
std::vector<evaluation::MseResult> readMseCsv(
  const std::string& path, int runID, int matchesPerPlayer, int interval
);
void writeMseCsv(
  const std::string& path, int runID, const std::vector<evaluation::MseResult>& results
);
// 保存済み一時ファイルを同一ファイルシステム内のハードリンクで公開する。
// 出力先があれば原子的に失敗する。成功後の一時ファイル削除は呼び出し側で行う。
void publishMseCsv(const std::string& temporaryPath, const std::string& outputPath);

// 観測した対戦履歴を先頭から順に毎試合更新し、各人evaluationIntervalMatchesPerPlayer試合ごとの
// チェックポイントで i < j の全ペアの予測勝率をCSV（elo.csv形式）へ出力する。
// 既存ファイルが存在する場合は保護のため例外を送出する。
void writeEloPredictions(
  int runID,
  const std::vector<int>& players,
  const std::vector<Match>& matches,
  int evaluationIntervalMatchesPerPlayer,
  const std::string& outputPath,
  const ratings::EloParameters& params = ratings::EloParameters{}
);

// conditional=trueでは履歴の条件で更新し、全ペアのz=±1を別々に出力する。
// falseは従来のS0動作確認用の両側平均。onlineLogには更新前の対戦予測を記録する。
void writeAdvantageEloPredictions(
  int runID, const std::vector<int>& players, const std::vector<Match>& matches,
  int interval, const std::string& outputPath,
  const ratings::AdvantageEloParameters& params = ratings::AdvantageEloParameters{},
  bool conditional = false, std::ostream* onlineLog = nullptr
);

}  // namespace project10::io
