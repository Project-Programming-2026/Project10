#include "project10/simulation/players.hpp"

#include <numeric>
#include <stdexcept>

// プレイヤーIDを0から連番で生成する。CSVへの保存はio側で行う。
std::vector<int> generatePlayers(int numPlayers) {
  if (numPlayers <= 0) {
    throw std::invalid_argument("numPlayers must be positive");
  }
  std::vector<int> players(static_cast<std::size_t>(numPlayers));
  std::iota(players.begin(), players.end(), 0);
  return players;
}
