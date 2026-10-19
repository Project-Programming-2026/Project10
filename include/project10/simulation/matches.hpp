#pragma once

#include <cstdint>
#include <vector>

struct Match {
  std::uint64_t matchID;
  int periodID;
  int playerA;
  int playerB;
  int resultA;
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
  std::uint64_t resultSeed
);
