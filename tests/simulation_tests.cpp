#include "project10/io/config.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/players.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Action>
void rejects(Action action) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("Expected invalid_argument");
}

}  // namespace

int main() {
  try {
    const auto players = generatePlayers(8);
    require(players.size() == 8, "Incorrect ID count");
    for (int id = 0; id < 8; id++) {
      require(players[id] == id, "Incorrect or duplicate player ID");
    }
    rejects([] { generatePlayers(0); });
    rejects([] { generatePlayers(-1); });
    const auto first = generateLatentParameters(8, 0.0, 1.0, 42);
    const auto repeated = generateLatentParameters(8, 0.0, 1.0, 42);
    require(first.size() == 8, "Incorrect player count");
    require(first == repeated, "Same seed did not reproduce abilities");
    require(first != generateLatentParameters(8, 0.0, 1.0, 43), "Seed was ignored");
    for (const double value : first) {
      require(std::isfinite(value), "Non-finite ability");
    }
    rejects([] { generateLatentParameters(0, 0.0, 1.0, 42); });
    rejects([] { generateLatentParameters(8, 0.0, 0.0, 42); });
    rejects([] { generateLatentParameters(8, 0.0, -1.0, 42); });
    rejects([] {
      generateLatentParameters(8, std::numeric_limits<double>::quiet_NaN(), 1.0, 42);
    });
    rejects([] {
      generateLatentParameters(8, 0.0, std::numeric_limits<double>::infinity(), 42);
    });
    require(project10::io::runDirectoryName(0) == "run_0000", "Zero padding failed");
    require(project10::io::runDirectoryName(12345) == "run_12345", "ID was truncated");
    rejects([] { project10::io::runDirectoryName(-1); });
    std::cout << "Simulation unit checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
