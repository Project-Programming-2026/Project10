#pragma once

#include <cstdint>
#include <vector>

struct Match {
  std::uint64_t matchID;
  int periodID;
  int playerA;
  int playerB;
  int resultA;
  int condition = 1;  // z=+1: Aが先手、z=-1: Bが先手。CSVでは別ファイルへ保存。
};

// parametersの添字がプレイヤーID。偶数人数・正の試合数と期間に限定する。
// 1ラウンドに各人1試合。端数ラウンドは短い最終期間として保存する。
// 潜在能力は生成専用とし、返す履歴には含めない。
std::vector<Match> generateMatches(
  const std::vector<double>& parameters,
  int matchesPerPlayer,
  int ratingPeriodMatchesPerPlayer,
  double performanceStd,
  std::uint64_t matchingSeed,
  std::uint64_t resultSeed,
  double sideAdvantage = 0, std::uint64_t conditionSeed = 0, bool conditional = false
);
