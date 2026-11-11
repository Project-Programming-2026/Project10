#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// 指定された平均と標準偏差の正規分布から潜在能力を生成する。
std::vector<double> generateLatentParameters(
  std::size_t numPlayers,
  double mean,
  double stddev,
  std::uint64_t seed
);
