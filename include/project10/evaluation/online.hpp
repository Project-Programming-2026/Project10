#pragma once

#include "project10/simulation/matches.hpp"
#include <string>
#include <vector>

namespace project10::evaluation {
struct OnlinePrediction {
  int runID;
  std::uint64_t matchID;
  int condition;
  int resultA;
  double elo;
  double advantage;
};
struct OnlineMetrics {
  int matchesPerPlayer;
  double eloLogLoss, advantageLogLoss, eloBrier, advantageBrier;
};
// 試合前予測を同じ履歴と照合し、各チェックポイントまでの累積平均を返す。
std::vector<OnlineMetrics> calculateOnlineMetrics(const std::string& path, int runID,
  const std::vector<Match>& matches, int numPlayers, int interval);
}  // namespace project10::evaluation
