#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// 標準正規分布を使用して潜在能力を生成する関数
std::vector<double> generateLatentParameters(
  std::size_t numPlayers,
  double mean,
  double stddev,
  std::uint64_t seed
);
