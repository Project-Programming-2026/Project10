#include "project10/io/csv.hpp"

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
