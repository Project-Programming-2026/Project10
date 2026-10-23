#include "project10/evaluation/run.hpp"
#include "project10/evaluation/plot.hpp"
#include "project10/simulation/run.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

void require(bool condition) {
  if (!condition) throw std::runtime_error("runサービスの契約違反");
}

void write(const fs::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary);
  file << text;
  file.close();
  require(static_cast<bool>(file));
}

std::string read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  require(static_cast<bool>(file));
  std::ostringstream text;
  text << file.rdbuf();
  require(!file.bad());
  return text.str();
}

template <typename Action>
void rejects(Action action, const std::string& message) {
  try { action(); }
  catch (const std::exception& error) {
    require(std::string(error.what()).find(message) != std::string::npos);
    return;
  }
  throw std::runtime_error("拒否すべき操作が成功しました");
}

void testServices(const std::string& sampleConfig) {
  auto config = nlohmann::json::parse(sampleConfig);
  config["num_players"] = 2;
  config["matches_per_player"] = 25;
  fs::create_directories("configs/run_0000");
  write("configs/run_0000/config.json", config.dump(2));
  const fs::path dir = "runs/run_0000";
  const fs::path lock = "runs/.run_0000.lock";
  int confirmations = 0;
  const auto generated = project10::simulation::generateRun(0, [&](const fs::path&) {
    ++confirmations;
    return true;
  });
  require(generated && generated->numPlayers == 2 && generated->outputDir == dir);
  require(confirmations == 0 && !fs::exists(lock));
  std::map<std::string, std::string> original;
  for (const auto& entry : fs::directory_iterator(dir)) {
    original.emplace(entry.path().filename().string(), read(entry.path()));
  }
  require(original.size() == 5);
  {
    const project10::io::RunLock borrowed(lock);
    require(!project10::simulation::generateRun(0, {}, &borrowed));
    require(fs::is_directory(lock));
    rejects([] { project10::simulation::generateRun(0); }, "このrunはすでに使用中です");
  }
  {
    const project10::io::RunLock wrong("runs/.run_0001.lock");
    rejects([&] { project10::simulation::generateRun(0, {}, &wrong); }, "対象runのロック");
    rejects([&] { project10::evaluation::evaluateRun(0, "Elo", &wrong); }, "対象runのロック");
    rejects([&] {
      project10::evaluation::plotRun(0, false, "graphs", "unused", std::cout, &wrong);
    }, "対象runのロック");
  }
  require(!fs::exists(lock) && !fs::exists("runs/.run_0001.lock"));
  write(dir / "keep.txt", "unrelated result");
  config["num_players"] = 4;
  write("configs/run_0000/config.json", config.dump(2));
  require(!project10::simulation::generateRun(0));  // 非対話利用でも既定は拒否。
  require(!project10::simulation::generateRun(0, [&](const fs::path& output) {
    ++confirmations;
    require(output == dir && fs::is_directory(lock));
    rejects([] { project10::simulation::generateRun(0); }, "このrunはすでに使用中です");
    rejects([] { project10::evaluation::evaluateRun(0, "Elo"); }, "このrunはすでに使用中です");
    return false;
  }));
  require(confirmations == 1 && !fs::exists(lock));
  for (const auto& [name, contents] : original) require(read(dir / name) == contents);
  rejects([] {
    project10::simulation::generateRun(0, [](const fs::path&) -> bool {
      throw std::runtime_error("confirmation failed");
    });
  }, "confirmation failed");
  require(!fs::exists(lock));
  // 最後の保存先が不正でも、先行するCSVを変更しない。
  fs::remove(dir / "config.json");
  fs::create_directory(dir / "config.json");
  rejects([] {
    project10::simulation::generateRun(0, [](const fs::path&) { return true; });
  }, "Output path must be a regular file:");
  for (const auto& [name, contents] : original) {
    if (name != "config.json") require(read(dir / name) == contents);
  }
  require(!fs::exists(lock) && read(dir / "keep.txt") == "unrelated result");
  fs::remove(dir / "config.json");
  write(dir / "config.json", original.at("config.json"));
  fs::remove_all("configs");  // 評価は保存済み設定を使用する。
  write(dir / "ground_truth.csv", "player_a,player_b,p_a_wins\n0,1,0.5\n");
  const std::string header = "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n";
  write(dir / "elo.csv", header + "0,10,0,1,0.75\n");
  rejects([] { project10::evaluation::evaluateRun(0, "Elo"); }, "予測ペアが不足しています");
  require(!fs::exists(lock) && !fs::exists(dir / "elo_mse.csv"));
  write(dir / "elo.csv", header + "0,20,0,1,0.5\n0,10,0,1,0.75\n");
  fs::create_directory(lock);
  rejects([] { project10::evaluation::evaluateRun(0, "Elo"); }, "このrunはすでに使用中です");
  require(fs::is_empty(lock));  // 他者のロックは削除しない。
  fs::remove(lock);
  const auto evaluated = project10::evaluation::evaluateRun(0, "Elo");
  require(evaluated.output == dir / "elo_mse.csv" && evaluated.cleanupWarning.empty());
  auto mse = read(evaluated.output);
  for (auto pos = mse.find('\r'); pos != std::string::npos; pos = mse.find('\r')) mse.erase(pos, 1);
  require(mse == "run_id,matches_per_player,mse\n0,10,0.0625\n0,20,0\n");
  require(!fs::exists(lock) && fs::hard_link_count(evaluated.output) == 1);
  const auto saved = read(evaluated.output);
  rejects([] { project10::evaluation::evaluateRun(0, "Elo"); }, "MSE出力先がすでに存在します");
  require(read(evaluated.output) == saved && !fs::exists(lock));
}

}  // namespace

int main(int argc, char** argv) {
  const auto previous = fs::current_path();
  try {
    require(argc == 3);
    const auto sample = read(argv[2]);
    const auto work = fs::absolute(argv[1]) / ("services-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
    require(fs::create_directories(work));
    fs::current_path(work);
    testServices(sample);
    fs::current_path(previous);
  } catch (const std::exception& error) {
    fs::current_path(previous);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
