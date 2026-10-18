#include "project10/io/csv.hpp"
#include "project10/simulation/probit.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

// CSV入出力の参考資料はdocs/references.mdを参照。
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

void writeMatches(const std::vector<Match>& matches, const std::string& outputPath) {
  std::ofstream file(outputPath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + outputPath);
  }
  file << "match_id,period_id,player_a,player_b,result_a\n";
  for (const auto& match : matches) {
    file << match.matchID << ',' << match.periodID << ',' << match.playerA << ','
      << match.playerB << ',' << match.resultA << '\n';
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
