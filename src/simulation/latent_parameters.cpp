#include "project10/simulation/latent_parameters.hpp"

#include <stdexcept>
#include <cmath>
#include <random>

// 標準正規分布を使用して潜在能力を生成する関数
std::vector<double> generateLatentParameters(
  std::size_t numPlayers,
  double mean,
  double stddev,
  std::uint64_t seed
) {
  // パラメータの検証
  if (numPlayers == 0) {
    throw std::invalid_argument("numPlayers must be positive");
  }

  if (!std::isfinite(mean) ||
      !std::isfinite(stddev) ||
      stddev <= 0.0) {
    throw std::invalid_argument("Invalid normal distribution parameters");
  }

  // 乱数生成器を初期化（mt19937というのが乱数生成器で、64ビットの符号なし整数を生成する）
  std::mt19937_64 rng(seed);

  // 正規分布を定義
  std::normal_distribution<double> dist(mean, stddev);

  // 潜在能力を生成
  std::vector<double> parameters;
  parameters.reserve(numPlayers);

  for (std::size_t i = 0; i < numPlayers; i++) {
    const double value = dist(rng);
    if (!std::isfinite(value)) {
      throw std::runtime_error("Generated ability must be finite");
    }
    parameters.push_back(value);
  }

  return parameters;
}
