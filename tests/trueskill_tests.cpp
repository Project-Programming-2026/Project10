#include "project10/io/csv.hpp"
#include "project10/ratings/trueskill.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace project10::ratings;

namespace {

void require(bool condition) {
  if (!condition) throw std::runtime_error("TrueSkillの検証に失敗しました。");
}

void near(double actual, long double expected, long double tolerance = 1e-12L) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
    throw std::runtime_error("数値が一致しません: " + std::to_string(actual));
  }
}

template <typename Action>
void rejects(Action action) {
  try { action(); } catch (const std::exception&) { return; }
  throw std::runtime_error("不正な入力が拒否されませんでした。");
}

void testRatings() {
  TrueSkillRating a, b;
  near(predictTrueSkill(a, b), 0.5L);
  const long double q = (25.0L / 3) * (25.0L / 3) + (25.0L / 300) * (25.0L / 300);
  const long double c2 = 2 * q + 2 * (25.0L / 6) * (25.0L / 6);
  const long double v = std::sqrt(2 / std::acos(-1.0L));
  updateTrueSkill(a, b, 1);
  near(a.mu, 25 + q / std::sqrt(c2) * v);
  near(b.mu, 25 - q / std::sqrt(c2) * v);
  near(a.sigma, std::sqrt(q * (1 - q / c2 * v * v)));
  near(b.sigma, a.sigma);
  require(predictTrueSkill(a, b) > 0.5);
  near(predictTrueSkill(a, b) + predictTrueSkill(b, a), 1);
  TrueSkillRating x{31, 3}, y{17, 7}, reversedX = x, reversedY = y;
  updateTrueSkill(x, y, 0);
  updateTrueSkill(reversedY, reversedX, 1);
  near(x.mu, reversedX.mu);
  near(y.mu, reversedY.mu);
  near(x.sigma, reversedX.sigma);
  near(y.sigma, reversedY.sigma);
  for (int i = 0; i < 10000; ++i) updateTrueSkill(a, b, i % 3 == 0);
  require(predictTrueSkill(a, b) >= 0 && predictTrueSkill(a, b) <= 1);
  rejects([&] { updateTrueSkill(a, a, 1); });
  rejects([&] { updateTrueSkill(a, b, 2); });
  for (const auto invalid : {TrueSkillRating{0, 0}, TrueSkillRating{0, -1},
        TrueSkillRating{NAN, 1}, TrueSkillRating{0, INFINITY},
        TrueSkillRating{0, std::numeric_limits<double>::max()}}) {
    rejects([&] { predictTrueSkill(invalid, b); });
    auto copy = invalid;
    const auto before = b;
    rejects([&] { updateTrueSkill(copy, b, 1); });
    near(b.mu, before.mu);
    near(b.sigma, before.sigma);
  }
}

void testTail() {
  for (const long double y : {9.999L, 10.0L, 10.001L, 20.0L, 1000.0L}) {
    std::array<long double, 3> moments{};
    constexpr int steps = 20000;
    const long double h = 60.0L / steps;
    for (int i = 0; i <= steps; ++i) {
      const long double u = i * h;
      const long double weight = (i == 0 || i == steps) ? 1 : (i % 2 == 0 ? 2 : 4);
      const long double density = weight * std::exp(-u - u * u / (2 * y * y));
      moments[0] += density;
      moments[1] += density * u;
      moments[2] += density * u * u;
    }
    const auto mean = moments[1] / moments[0];
    const auto variance = moments[2] / moments[0] - mean * mean;
    const auto v = y + mean / y;
    const auto w = 1 - variance / (y * y);
    const long double q = (25.0L / 3) * (25.0L / 3) + (25.0L / 300) * (25.0L / 300);
    const long double c2 = 2 * q + 2 * (25.0L / 6) * (25.0L / 6);
    TrueSkillRating a{static_cast<double>(-y * std::sqrt(c2)), 25.0 / 3}, b{0, 25.0 / 3};
    const auto mu = a.mu;
    updateTrueSkill(a, b, 1);
    near(a.mu, mu + q / std::sqrt(c2) * v, 2e-10L);
    near(b.mu, -q / std::sqrt(c2) * v, 2e-10L);
    near(a.sigma, std::sqrt(q * (1 - q / c2 * w)), 2e-10L);
  }
  TrueSkillRating a{-1e10, 1}, b{1e10, 1};
  near(predictTrueSkill(a, b), 0);
  updateTrueSkill(a, b, 1);
  require(std::isfinite(a.mu) && a.sigma > 0 && std::isfinite(a.sigma));
}

void testCsv(const std::filesystem::path& path) {
  const auto write = [&](const std::string& contents) {
    std::ofstream file(path, std::ios::binary);
    file << contents;
  };
  const auto read = [&] { return project10::io::readMatchesCsv(path.string(), 2, 2, 1); };
  const std::string header = "match_id,period_id,player_a,player_b,result_a\n";
  write("match_id,period_id,player_a,player_b,result_a\r\n0,0,0,1,1\r\n1,1,1,0,0\r\n");
  require(read().size() == 2);
  for (const auto& rows : {"0,0,0,1,1\n", "0,0,0,1,1\n0,1,0,1,0\n",
        "1,0,0,1,1\n1,1,0,1,0\n", "0,-1,0,1,1\n", "0,1,0,1,1\n",
        "0,0,0,0,1\n", "0,0,0,2,1\n", "0,0,-1,1,1\n", "0,0,0,1,2\n",
        "0,0,0,1,0.5\n", "0x,0,0,1,1\n", "0,0,0,1,1,\n", "\n",
        "18446744073709551616,0,0,1,1\n", "0,0,0,1,nan\n",
        "0,0,0,1,1\n1,1,0,1,0\n2,2,0,1,0\n"}) {
    write(header + rows);
    rejects(read);
  }
  write("wrong\n0,0,0,1,1\n1,1,0,1,0\n");
  rejects(read);
  std::vector<Match> matches;
  for (int round = 0; round < 5; ++round) {
    for (int a = 0; a < 4; a += 2) {
      matches.push_back({matches.size(), round / 3, a, a + 1, round % 2});
    }
  }
  std::ostringstream output, repeat, prefix;
  writeTrueSkillPredictions(output, 7, 4, 2, matches);
  writeTrueSkillPredictions(repeat, 7, 4, 2, matches);
  require(output.str() == repeat.str());
  writeTrueSkillPredictions(prefix, 7, 4, 2, {matches.begin(), matches.begin() + 4});
  require(output.str().substr(0, prefix.str().size()) == prefix.str());
  write(output.str());
  project10::evaluation::ProbabilityMatrix truth(4, std::vector<double>(4, 0.5));
  project10::evaluation::MseEvaluator evaluator(7, 5, 2, truth);
  project10::io::readPredictionsCsv(path.string(), evaluator);
  require(evaluator.finish().size() == 2);
  for (int i = 0; i < 8; ++i) {
    matches[i].playerA = i < 4 ? 0 : 2;
    matches[i].playerB = i < 4 ? 1 : 3;
  }
  rejects([&] { writeTrueSkillPredictions(repeat, 7, 4, 2, matches); });
  rejects([&] { writeTrueSkillPredictions(repeat, -1, 4, 2, matches); });
  std::ostringstream failed;
  failed.setstate(std::ios::badbit);
  rejects([&] { writeTrueSkillPredictions(failed, 7, 4, 2, {}); });
}

}

int main(int argc, char** argv) {
  try {
    require(argc == 2);
    std::filesystem::create_directories(argv[1]);
    testRatings();
    testTail();
    testCsv(std::filesystem::path(argv[1]) / "trueskill-unit.csv");
    std::cout << "TrueSkillの数値・CSV検証に成功しました。\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
