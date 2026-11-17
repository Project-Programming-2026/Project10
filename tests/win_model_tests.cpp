#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/simulation/probit.hpp"
#include "project10/simulation/run.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace sim = project10::simulation;
namespace io = project10::io;
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a, double b) {
  require(std::isfinite(a) && std::abs(a - b) < 1e-12, "勝率が独立した期待値と一致しません");
}
template<class F> void rejects(F action) {
  try { action(); } catch (const std::exception&) { return; }
  throw std::runtime_error("不正な入力・上書きが拒否されませんでした");
}
std::string read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  require(static_cast<bool>(file), "ファイルを開けません");
  std::ostringstream text; text << file.rdbuf(); return text.str();
}
void write(const fs::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary); file << text; file.close();
  require(static_cast<bool>(file), "ファイルを保存できません");
}
void probabilities() {
  const double l = std::log(3.0), big = std::numeric_limits<double>::max();
  const double tiny = std::numeric_limits<double>::denorm_min();
  const sim::WinProbabilityModel model = sim::LogisticModel{1};
  require(model == sim::WinProbabilityModel{sim::LogisticModel{1}} &&
          model != sim::WinProbabilityModel{sim::LogisticModel{2}} &&
          model != sim::WinProbabilityModel{sim::ProbitModel{1}}, "モデルの比較が不正です");
  near(sim::winProbability(l, 0, model), .75);  // exp(log(3))=3、3/(1+3)。
  near(sim::winProbability(0, l, model), .25);
  near(sim::winProbability(l * 2, 0, sim::LogisticModel{2}), .75);
  near(sim::winProbability(0, 0, model, l, 1), .75);
  near(sim::winProbability(0, 0, model, l, -1), .25);
  near(sim::winProbability(0, 0, model, l, 0), .5);
  near(sim::winProbability(l, 0, model, l, 0), .7);  // (9/10+1/2)/2、sigmoid(l)とは異なる。
  near(sim::winProbability(big, -big, sim::LogisticModel{big}), .8807970779778824);
  near(sim::winProbability(big, -big, sim::LogisticModel{big}, l, -1), .8807970779778824);
  near(sim::winProbability(-big, big, sim::LogisticModel{big}, l, 1), .1192029220221176);
  near(sim::winProbability(big, -big, sim::LogisticModel{big}, big, 1), .9525741268224332);
  near(sim::winProbability(big, big, model, l, 1), .75);  // 巨大な能力が相殺される。
  near(sim::winProbability(big, l, model, big, -1), .25);
  near(sim::winProbability(tiny, 0, sim::LogisticModel{tiny}), .7310585786300049);
  near(sim::winProbability(big, -big, model), 1);
  near(sim::winProbability(-big, big, model), 0);
  for (double scale : {tiny, 1.0, big}) {
    const sim::WinProbabilityModel m = sim::LogisticModel{scale};
    double previous = -1;
    for (double a : {-big, -l, 0.0, l, big}) {
      const double p = sim::winProbability(a, 0, m);
      require(std::isfinite(p) && p >= previous && p >= 0 && p <= 1, "単調性・確率範囲の違反");
      near(p + sim::winProbability(0, a, m), 1); previous = p;
      near(sim::winProbability(a, 0, m, l, 1) + sim::winProbability(0, a, m, l, -1), 1);
      require(sim::winProbability(a, 0, m, 0, -1) == p, "S0が通常勝率と異なります");
      require(sim::winProbability(a, 0, sim::ProbitModel{scale}) ==
              calculateWinProbability(a, 0, scale), "既存プロビットと一致しません");
    }
  }
  for (double invalid : {0.0, -1.0, -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
    rejects([&] { sim::winProbability(0, 0, sim::LogisticModel{invalid}); });
    rejects([&] { generateMatches({0, 0}, 2, 1, sim::LogisticModel{invalid}, 0, 0); });
    write("invalid-truth.csv", "protected output\n");
    rejects([&] { writeGroundTruth({0, 0}, sim::LogisticModel{invalid}, "invalid-truth.csv"); });
    require(read("invalid-truth.csv") == "protected output\n", "不正尺度で正解CSVが更新されました");
  }
  for (double invalid : {std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    rejects([&] { sim::winProbability(invalid, 0, model); });
    rejects([&] { sim::winProbability(0, invalid, model); });
    rejects([&] { sim::winProbability(0, 0, model, invalid, 0); });
  }
  rejects([&] { sim::winProbability(0, 0, model, -1); });
  for (int z : {-2, 2}) rejects([&] { sim::winProbability(0, 0, model, 0, z); });
}
void streams() {
  const double l = std::log(3.0);
  const std::vector<double> abilities{0, l, 2*l, 3*l}, strengths{1, 3, 9, 27};
  const auto a = generateMatches(abilities, 20, 10, sim::LogisticModel{1}, 42, 43, l, 44, true);
  const auto b = generateMatches(abilities, 20, 10, sim::ProbitModel{1}, 42, 43, l, 44, true);
  const auto repeat = generateMatches(abilities, 20, 10, sim::LogisticModel{1}, 42, 43, l, 44, true);
  std::mt19937_64 results(43);
  for (std::size_t i = 0; i < a.size(); ++i) {
    require(a[i].playerA == b[i].playerA && a[i].playerB == b[i].playerB &&
      a[i].condition == b[i].condition && a[i].periodID == b[i].periodID, "モデルがペア・条件の乱数へ影響しました");
    require(a[i].resultA == repeat[i].resultA && a[i].condition == repeat[i].condition,
      "勝敗・条件を再現できません");
    const double wa = strengths[a[i].playerA] * (a[i].condition == 1 ? 3 : 1.0/3);
    const double wb = strengths[a[i].playerB];
    require(a[i].resultA == (std::bernoulli_distribution(wa/(wa+wb))(results) ? 1 : 0),
      "独立したBradley-Terry勝率と勝敗乱数の接続が一致しません");
  }
  const auto normal = generateMatches(abilities, 20, 10, sim::LogisticModel{1}, 42, 43);
  const auto s0 = generateMatches(abilities, 20, 10, sim::LogisticModel{1}, 42, 43, 0, 44, true);
  for (std::size_t i = 0; i < normal.size(); ++i)
    require(normal[i].resultA == s0[i].resultA, "S0で勝敗乱数の消費が変わりました");
}
void files(json config) {
  fs::create_directories("configs/run_0000");
  config["run_id"] = 0; config["num_players"] = 2;
  config["matches_per_player"] = 20;
  const json probit = config;
  config["model"] = "logistic_1d"; config.erase("performance_std"); config["logistic_scale"] = 1;
  const fs::path path = "configs/run_0000/config.json";
  const auto load = [&](const json& value) {
    write(path, value.dump(2)); return io::loadConfig(0);
  };
  require(std::holds_alternative<sim::ProbitModel>(load(probit).winModel), "旧設定が読めません");
  require(std::holds_alternative<sim::LogisticModel>(load(config).winModel), "新設定が読めません");
  for (const char* field : {"model", "logistic_scale"}) {
    auto invalid = config; invalid.erase(field); rejects([&] { load(invalid); });
  }
  for (json value : {json(0), json(-1), json("1"), json(nullptr), json(true)}) {
    auto invalid = config; invalid["logistic_scale"] = value; rejects([&] { load(invalid); });
  }
  auto both = config; both["performance_std"] = 1; rejects([&] { load(both); });
  both = probit; both["logistic_scale"] = 1; rejects([&] { load(both); });
  auto unknown = config; unknown["model"] = "logistic"; rejects([&] { load(unknown); });
  write(path, "{\"model\":\"logistic_1d\"," + config.dump().substr(1));
  rejects([&] { io::loadConfig(0); });
  load(config);
  require(sim::generateRun(0).has_value(), "ロジスティックのrun生成に失敗しました");
  const auto saved = read("runs/run_0000/config.json"), truth = read("runs/run_0000/ground_truth.csv");
  const auto protectedRun = [&](int id) {
    rejects([&] { sim::generateRun(id); });
    int confirmations = 0;
    for (bool answer : {false, true})
      rejects([&] { sim::generateRun(id, [&](const auto&) { ++confirmations; return answer; }); });
    require(confirmations == 0, "保護されたrunで上書き確認が呼ばれました");
  };
  protectedRun(0);
  load(probit); protectedRun(0);
  require(read("runs/run_0000/config.json") == saved && read("runs/run_0000/ground_truth.csv") == truth,
    "既存のロジスティックrunが変更されました");
  fs::create_directories("configs/run_0001");
  auto next = probit; next["run_id"] = 1;
  write("configs/run_0001/config.json", next.dump());
  require(sim::generateRun(1).has_value(), "プロビットのrun生成に失敗しました");
  const auto old = read("runs/run_0001/config.json");
  next = config; next["run_id"] = 1;
  write("configs/run_0001/config.json", next.dump());
  protectedRun(1);
  require(read("runs/run_0001/config.json") == old, "既存プロビットrunが変更されました");
  // 正解出力と共通MSEを、生成した能力に依存しない既知の2人で照合する。
  writeGroundTruth({std::log(3.0), 0}, sim::LogisticModel{1}, "truth.csv");
  const auto values = io::readGroundTruthCsv("truth.csv", 2);
  near(values[0][1], .75);
  project10::evaluation::MseEvaluator mse(0, 1, 1, values);
  mse.addPrediction({0, 1, 0, 1, values[0][1]}); near(mse.finish()[0].mse, 0);
  writeGroundTruth({std::log(3.0), 0}, sim::LogisticModel{1}, "mixed.csv", std::log(3.0));
  near(io::readGroundTruthCsv("mixed.csv", 2)[0][1], .7);
  for (int z : {-1, 1}) {
    writeGroundTruth({0, 0}, sim::LogisticModel{1}, "side.csv", std::log(3.0), z);
    near(io::readGroundTruthCsv("side.csv", 2)[0][1], z == 1 ? .75 : .25);
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 3, "テスト用パスと設定を指定してください");
    const auto config = json::parse(read(argv[2]));
    const auto root = fs::path(argv[1]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root); fs::current_path(root);
    probabilities(); streams(); files(config);
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
