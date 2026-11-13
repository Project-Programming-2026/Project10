#include "project10/io/csv.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace io = project10::io;
void require(bool condition) {
  if (!condition) throw std::runtime_error("6手法アダプターの検証に失敗しました。");
}
template<class F> void rejects(F action) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  require(rejected);
}
int main(int argc, char** argv) {
  try {
    require(argc == 2);
    const fs::path root(argv[1]);
    fs::create_directories(root);
    const auto path = (root / "advantage.csv").string();
    fs::remove(path);
    const std::vector<Match> matches{{0, 0, 0, 1, 1}};
    project10::ratings::AdvantageEloParameters params;
    params.initialAdvantage = 80;
    params.kAdvantage = 0;
    io::writeAdvantageEloPredictions(0, {0, 1}, matches, 1, path, params);
    // 1試合を独立に計算: p_old=logistic10(80/400)、レート差'=2*32*(1-p_old)。
    const double oldP = 1.0 / (1.0 + std::pow(10.0, -0.2));
    const double difference = 64.0 * (1.0 - oldP);
    const double positive = 1.0 / (1.0 + std::pow(10.0, -(difference + 80.0) / 400.0));
    const double negative = 1.0 / (1.0 + std::pow(10.0, -(difference - 80.0) / 400.0));
    const double mixture = 0.5 * (positive + negative);
    project10::evaluation::MseEvaluator evaluator(0, 1, 1, {{0, mixture}, {0, 0}});
    io::readPredictionsCsv(path, evaluator);
    require(evaluator.finish().at(0).mse < 1e-25);
    rejects([&] { io::writeAdvantageEloPredictions(0, {0, 1}, matches, 1, path); });
    fs::remove(path);
    params.initialAdvantage = 0;
    params.kAdvantage = 0;
    io::writeAdvantageEloPredictions(0, {0, 1}, matches, 1, path, params);
    const auto elo = (root / "elo.csv").string();
    fs::remove(elo);
    io::writeEloPredictions(0, {0, 1}, matches, 1, elo);
    std::ifstream a(path), b(elo);
    std::stringstream first, second;
    first << a.rdbuf(); second << b.rdbuf();
    require(first.str() == second.str());
    const auto reference = (root / "rating.csv").string();
    for (const auto& invalid : {"wrong\n1500\n1500\n", "rating\nnan\n1500\n",
                                "rating\ninf\n1500\n", "rating\n1500\n",
                                "rating\n1500\n1500\n1500\n"}) {
      std::ofstream(reference) << invalid;
      rejects([&] { io::readGlicko2RatingsCsv(reference, 2); });
    }
    std::ofstream(reference) << "rating\r\n1500\r\n1500\r\n";
    require(io::readGlicko2RatingsCsv(reference, 2) == std::vector<double>({1500, 1500}));
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
