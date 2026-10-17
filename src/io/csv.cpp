#include "project10/io/csv.hpp"
#include "project10/simulation/probit.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

// 参考：Qiita「C++でCSVファイルを読み書きする方法」
// https://qiita.com/shirosuke_93/items/d5d068bb15c8e8817c34
void writePlayers(const std::vector<int>& players, const std::string& outputPath) {
  std::ofstream file(outputPath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + outputPath);
  }
  file << "player_id\n";
  for (const int player : players) {
    file << player << '\n';
  }
  file.close();
  if (!file) {
    throw std::runtime_error("Failed to write: " + outputPath);
  }
}

void writeLatentParameters(
  const std::vector<double>& parameters,
  const std::string& outputPath
) {
  std::ofstream file(outputPath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + outputPath);
  }
  file << "player_id,theta_0\n";
  file << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (std::size_t i = 0; i < parameters.size(); i++) {
    file << i << ',' << parameters[i] << '\n';
  }
  file.close();
  if (!file) {
    throw std::runtime_error("Failed to write: " + outputPath);
  }
}

void writeGroundTruth(
  const std::vector<double>& parameters,
  double performanceStd,
  const std::string& outputPath
) {
  std::ofstream file(outputPath);

  if (!file.is_open()) {
    throw std::runtime_error(
      "Failed to open: " + outputPath
    );
  }

  file << "player_a,player_b,p_a_wins\n";
  file << std::setprecision(
    std::numeric_limits<double>::max_digits10
  );

  // 自己対戦を除外しつつ、逆順の組み合わせも除外するようなループを回す
  for (std::size_t i = 0; i < parameters.size(); i++) {
    for (std::size_t j = i + 1; j < parameters.size(); j++) {
      const double probability = calculateWinProbability(
        parameters[i],
        parameters[j],
        performanceStd
      );

      file << i << ',' << j << ','
        << probability << '\n';
    }
  }

  file.close();

  if (!file) {
    throw std::runtime_error(
      "Failed to write: " + outputPath
    );
  }
}
