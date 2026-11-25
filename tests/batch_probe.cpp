// テスト専用: 推定器へ正解情報を渡さず、親のrunロックを全工程で保持することを観測する。
#include "project10/io/process.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/io/config.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

namespace fs = std::filesystem;

void waitForPeer(const fs::path& path) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!fs::exists(path)) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("並列実行する他の手法が起動しませんでした。");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

int main(int argc, char** argv) {
  try {
    if (argc != 3 && argc != 1) return 1;
    const bool interactive = argc == 1;
    const bool render = !interactive && std::string(argv[1]) == "-d";
    const auto cwd = fs::current_path();
    const auto method = interactive ? cwd.filename().string() : std::string(argv[2]);
    int runID = 0;
    if (interactive && !(std::cin >> runID)) return 1;
    if (!interactive && !render) runID = std::stoi(argv[1]);
    const auto run = render ? cwd.parent_path() : cwd / "runs" /
      project10::io::runDirectoryName(runID);
    const auto lock = render ? run.parent_path() / ("." + run.filename().string() + ".lock") :
      cwd.parent_path().parent_path().parent_path();
    const auto checkLock = [&] {
      if (!fs::is_directory(lock)) throw std::runtime_error("親のrunロックがありません。");
      try { const project10::io::RunLock other(lock); }
      catch (const std::exception&) { return; }
      throw std::runtime_error("親のrunロックがありません。");
    };
    checkLock();
    if (render) {
      std::ofstream("mse.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
      std::ifstream script("plot.gp");
      const std::string text((std::istreambuf_iterator<char>(script)), {});
      const bool png = text.find("mse.png") != std::string::npos;
      if (png) std::ofstream("mse.png", std::ios::binary) << "\x89PNG\r\n\x1a\nfixture";
      for (const std::string stem : {"online_log_loss", "online_brier", "ranking_kendall",
                                    "ranking_weighted_kendall", "ranking_normalized_weighted_kendall"}) {
        if (text.find(stem + ".svg") != std::string::npos)
          std::ofstream(stem + ".svg") << "<svg></svg>";
        if (text.find(stem + ".png") != std::string::npos)
          std::ofstream(stem + ".png", std::ios::binary) << "\x89PNG\r\n\x1a\nfixture";
      }
    } else {
      if (fs::exists(run / "ground_truth.csv") || fs::exists(run / "latent_parameters.csv"))
        throw std::runtime_error("推定器の作業先に正解情報があります。");
      const bool ranked = method == "rankedGlicko2";
      const bool conditional = method == "AdvantageElo" && fs::exists(run / "conditions.csv");
      if (ranked != fs::is_regular_file(cwd / "samples/glicko_baseline/glicko2_rating.csv"))
        throw std::runtime_error("固定参照の配置先が不正です。");
      if (std::distance(fs::directory_iterator(run), fs::directory_iterator{}) != (conditional ? 4 : 3))
        throw std::runtime_error("推定器の入力は設定・ID・対戦履歴だけにしてください。");
      std::ifstream snapshot(run / "config.json");
      const std::string settings((std::istreambuf_iterator<char>(snapshot)), {});
      for (const auto* name : {"side_advantage", "condition_seed", "ability_distribution", "performance_std",
                               "logistic_scale", "\"model\"", "\"seeds\""})
        if (settings.find(name) != std::string::npos)
          throw std::runtime_error("推定器へ真の生成条件を渡しています。");
      const auto observe = lock.parent_path().parent_path() / "observe";
      if (fs::is_directory(observe / "parallel")) {
        fs::create_directory(observe / (method + "-started"));
        if (method == "Elo" || method == "Glicko") {
          waitForPeer(observe / (method == "Elo" ? "Glicko-started" : "Elo-started"));
          if (fs::exists(cwd.parent_path() / "Glicko2"))
            throw std::runtime_error("3手法目の作業先が並列枠の解放前に作成されました。");
          if (fs::exists(observe / "fail-elo")) {
            if (method == "Elo") {
              fs::create_directory(observe / "Elo-failing");
              throw std::runtime_error("テスト用のElo予測失敗");
            }
            waitForPeer(observe / "Elo-failing");
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            checkLock();
          }
        } else if (!fs::exists(observe / "Elo-done") || !fs::exists(observe / "Glicko-done")) {
          throw std::runtime_error("最初の並列枠が空く前に次の手法が起動しました。");
        }
      }
      auto real = fs::path(argv[0]).parent_path() /
        (interactive ? "real_project10_glicko" : "real_project10_predict");
#ifdef _WIN32
      real += ".exe";
#endif
      if (interactive)
        project10::io::runProcess(real.string(), {}, cwd, lock / "predictions/stdin.txt");
      else
        project10::io::runProcess(real.string(), {argv[1], argv[2]}, cwd);
      if (conditional && fs::exists(observe / "collide-online")) {
        const auto destination = lock.parent_path() / run.filename() / "online_predictions.csv";
        std::ofstream file(destination);
        file << "fixture\n";
        file.close();
        if (!file) throw std::runtime_error("試合前予測CSVの衝突を準備できませんでした。");
      }
      if (fs::is_directory(observe / "parallel")) fs::create_directory(observe / (method + "-done"));
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
