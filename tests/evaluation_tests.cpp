#include "project10/evaluation/mse.hpp"
#include "project10/io/csv.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace project10::evaluation;

namespace {

void require(bool condition) {
  if (!condition) throw std::runtime_error("MSE評価のテストに失敗しました。");
}

template <typename Action>
void rejects(Action action) {
  try { action(); }
  catch (const std::exception&) { return; }
  throw std::runtime_error("不正な入力が受け付けられました。");
}

void write(const fs::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary);
  file << text;
  file.close();
  require(static_cast<bool>(file));
}

void testCalculation() {
  ProbabilityMatrix truth(4, std::vector<double>(4, 0.5));
  MseEvaluator evaluator(7, 25, 10, truth);
  rejects([&] { evaluator.finish(); });
  // 評価時点とペアを逆順・交互に入力。初回は(0.75-0.5)^2=1/16、次は0。
  for (int a = 2; a >= 0; --a) {
    for (int b = 3; b > a; --b) {
      evaluator.addPrediction({7, 20, a, b, 0.5});
      evaluator.addPrediction({7, 10, a, b, 0.75});
    }
  }
  const auto results = evaluator.finish();
  require(results.size() == 2 && results[0].matchesPerPlayer == 10 &&
          results[1].matchesPerPlayer == 20);
  require(results[0].mse == 0.0625 && results[1].mse == 0.0);
  rejects([&] { evaluator.addPrediction({7, 10, 0, 1, 0.75}); });
  for (const auto& row : std::vector<PredictionRow>{
         {8, 10, 0, 1, 0.5}, {7, 0, 0, 1, 0.5}, {7, 25, 0, 1, 0.5},
         {7, 30, 0, 1, 0.5}, {7, 10, -1, 1, 0.5}, {7, 10, 1, 1, 0.5},
         {7, 10, 2, 1, 0.5}, {7, 10, 0, 4, 0.5}, {7, 10, 0, 1, -0.1},
         {7, 10, 0, 1, 1.1}, {7, 10, 0, 1, std::numeric_limits<double>::quiet_NaN()},
         {7, 10, 0, 1, std::numeric_limits<double>::infinity()}}) {
    MseEvaluator invalid(7, 25, 10, truth);
    rejects([&] { invalid.addPrediction(row); });
  }
  rejects([&] { MseEvaluator invalid(7, 5, 10, truth); });
  rejects([&] { MseEvaluator invalid(7, 25, 0, truth); });
  rejects([] { MseEvaluator invalid(7, 25, 10, {{0.5}}); });
  truth[0].pop_back();
  rejects([&] { MseEvaluator invalid(7, 25, 10, truth); });
  truth[0].push_back(std::numeric_limits<double>::quiet_NaN());
  rejects([&] { MseEvaluator invalid(7, 25, 10, truth); });

  // ペアごとに異なる正解で添字の取り違えを検出。各誤差は1/8なのでMSEは1/64。
  for (int a = 0; a < 4; ++a)
    for (int b = a + 1; b < 4; ++b) truth[a][b] = (a + b + 1) / 8.0;
  MseEvaluator varied(7, 10, 10, truth);
  for (int a = 0; a < 4; ++a)
    for (int b = a + 1; b < 4; ++b)
      varied.addPrediction({7, 10, a, b, truth[a][b] + 0.125});
  require(varied.finish()[0].mse == 0.015625);
  // 二乗誤差は0.01,0,0.01,0.04,0.09,0.16。平均は31/600。
  MseEvaluator forward(0, 10, 10, ProbabilityMatrix(4, std::vector<double>(4, 0.5)));
  MseEvaluator reverse(0, 10, 10, ProbabilityMatrix(4, std::vector<double>(4, 0.5)));
  const std::vector<PredictionRow> mixed = {
    {0, 10, 0, 1, 0.4}, {0, 10, 0, 2, 0.5}, {0, 10, 0, 3, 0.6},
    {0, 10, 1, 2, 0.7}, {0, 10, 1, 3, 0.8}, {0, 10, 2, 3, 0.9}
  };
  for (const auto& row : mixed) forward.addPrediction(row);
  for (auto row = mixed.rbegin(); row != mixed.rend(); ++row) reverse.addPrediction(*row);
  require(std::abs(forward.finish()[0].mse - 31.0 / 600.0) < 1e-12);
  require(std::abs(forward.finish()[0].mse - reverse.finish()[0].mse) < 1e-12);
  MseEvaluator boundary(0, 1, 1, {{0.0, 0.0}, {1.0, 0.0}});
  boundary.addPrediction({0, 1, 0, 1, 1.0});
  require(boundary.finish()[0].mse == 1.0);

  MseEvaluator scale(0, 10, 10, ProbabilityMatrix(1000, std::vector<double>(1000, 0.5)));
  std::size_t pairs = 0;
  for (int a = 0; a < 1000; ++a) {
    for (int b = a + 1; b < 1000; ++b) {
      scale.addPrediction({0, 10, a, b, 0.75});
      ++pairs;
    }
  }
  require(pairs == 499500 && scale.finish()[0].mse == 0.0625);
}

void testCsv(const fs::path& dir) {
  const auto players = dir / "players.csv";
  write(players, "player_id\r\n3\r\n1\r\n0\r\n2\r\n");
  project10::io::validatePlayersCsv(players.string(), 4);
  for (const auto& rows : {"0\n1\n2\n", "0\n1\n1\n3\n", "0\n1\n2\n4\n", "0,\n", "\n"}) {
    write(players, std::string("player_id\n") + rows);
    rejects([&] { project10::io::validatePlayersCsv(players.string(), 4); });
  }
  const auto truthPath = dir / "truth.csv";
  const std::string header = "player_a,player_b,p_a_wins\n";
  const std::string pairs = "0,1,0.25\n0,2,0.5\n0,3,0.75\n1,2,0.5\n1,3,0.25\n2,3,0.5\n";
  write(truthPath, header + pairs);
  const auto truth = project10::io::readGroundTruthCsv(truthPath.string(), 4);
  require(truth[0][1] == 0.25 && truth[1][0] == 0.75 && std::isnan(truth[0][0]));
  for (const auto& rows : {"", "0,1,0.5\n", "0,0,0.5\n", "1,0,0.5\n", "0,4,0.5\n",
                          "0,1,nan\n", "0,1,inf\n", "0,1,1.01\n", "0,1,-0.1\n",
                          "0,1,0.5junk\n", "0.0,1,0.5\n", "0,1,\n", "0,1,0.5,\n"}) {
    write(truthPath, header + rows);
    rejects([&] { project10::io::readGroundTruthCsv(truthPath.string(), 4); });
  }
  write(truthPath, header + pairs + "0,1,0.25\n");
  rejects([&] { project10::io::readGroundTruthCsv(truthPath.string(), 4); });
  const auto predictions = dir / "predictions.csv";
  const std::string predictionHeader =
    "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n";
  std::string predictionRows;
  for (int a = 0; a < 4; ++a)
    for (int b = a + 1; b < 4; ++b)
      predictionRows += "7,10," + std::to_string(a) + "," + std::to_string(b) + "," +
                        std::to_string(truth[a][b]) + "\n";
  write(predictions, predictionHeader + predictionRows);
  MseEvaluator evaluator(7, 10, 10, truth);
  project10::io::readPredictionsCsv(predictions.string(), evaluator);
  const auto results = evaluator.finish();
  require(results[0].mse == 0.0);
  for (const auto& row : {"7,10,0,1,0.25,\n", "7x,10,0,1,0.25\n", "7,10,0,1,\n"}) {
    write(predictions, predictionHeader + row);
    MseEvaluator invalid(7, 10, 10, truth);
    rejects([&] { project10::io::readPredictionsCsv(predictions.string(), invalid); });
  }
  write(predictions, "wrong_header\n");
  rejects([&] { project10::io::readPredictionsCsv(predictions.string(), evaluator); });
  const auto output = dir / "mse.csv";
  project10::io::writeMseCsv(output.string(), 7, results);
  rejects([&] { project10::io::writeMseCsv(output.string(), 7, results); });
  std::ifstream file(output);
  std::ostringstream contents;
  contents << file.rdbuf();
  require(contents.str() == "run_id,matches_per_player,mse\n7,10,0\n");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 2);
    const auto dir = fs::path(argv[1]) / ("evaluation-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir.parent_path());
    require(fs::create_directory(dir));
    testCalculation();
    testCsv(dir);
    std::cout << "MSEの単体・CSV確認に成功しました\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
