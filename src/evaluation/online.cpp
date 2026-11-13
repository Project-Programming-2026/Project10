#include "project10/evaluation/online.hpp"
#include "project10/io/csv.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace project10::evaluation {
std::vector<OnlineMetrics> calculateOnlineMetrics(const std::string& path, int runID,
  const std::vector<Match>& matches, int numPlayers, int interval) {
  if (numPlayers < 2 || numPlayers % 2 != 0 || interval <= 0 || matches.empty() ||
      matches.size() % static_cast<std::size_t>(numPlayers / 2) != 0)
    throw std::invalid_argument("試合前評価の条件が不正です。");
  const auto checkpoint = static_cast<std::size_t>(numPlayers / 2) * interval;
  std::size_t count = 0;
  double logElo = 0, logAdv = 0, brierElo = 0, brierAdv = 0;
  std::vector<OnlineMetrics> metrics;
  io::readOnlinePredictionsCsv(path, [&](const OnlinePrediction& row) {
    if (count >= matches.size() || row.runID != runID || row.matchID != count ||
        matches[count].matchID != row.matchID ||
        row.condition != matches[count].condition || row.resultA != matches[count].resultA)
      throw std::runtime_error("試合前予測ログの対戦ID・条件・勝敗が履歴と一致しません。");
    const auto loss = [&](double probability) {
      // Log Lossに限って端点を安定化。保存値とBrier Scoreには適用しない。
      const double p = std::clamp(probability, 1e-15, 1.0 - 1e-15);
      return row.resultA ? -std::log(p) : -std::log1p(-p);
    };
    logElo += loss(row.elo); logAdv += loss(row.advantage);
    brierElo += std::pow(row.elo - row.resultA, 2);
    brierAdv += std::pow(row.advantage - row.resultA, 2);
    if (++count % checkpoint == 0)
      metrics.push_back({static_cast<int>(count / (numPlayers / 2)), logElo / count,
        logAdv / count, brierElo / count, brierAdv / count});
  });
  if (count != matches.size()) throw std::runtime_error("試合前予測ログに欠落があります。");
  return metrics;
}
}  // namespace project10::evaluation
