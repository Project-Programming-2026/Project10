// テスト専用: 推定器へ正解情報を渡さず、親のrunロックを全工程で保持することを観測する。
#include "project10/io/process.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/io/config.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;
int main(int argc, char** argv) {
  try {
    if (argc != 3) return 1;
    const bool render = std::string(argv[1]) == "-d";
    const auto cwd = fs::current_path();
    const auto run = render ? cwd.parent_path() : cwd / "runs" /
      project10::io::runDirectoryName(std::stoi(argv[1]));
    const auto lock = render ? run.parent_path() / ("." + run.filename().string() + ".lock") : cwd.parent_path();
    bool busy = false;
    try { const project10::io::RunLock other(lock); }
    catch (const std::exception&) { busy = true; }
    if (!busy) throw std::runtime_error("親のrunロックがありません。");
    if (render) {
      std::ofstream("mse.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
      std::ifstream script("plot.gp");
      const std::string text((std::istreambuf_iterator<char>(script)), {});
      const bool png = text.find("mse.png") != std::string::npos;
      if (png) std::ofstream("mse.png", std::ios::binary) << "\x89PNG\r\n\x1a\nfixture";
      if (text.find("$online") != std::string::npos) {
        for (const auto* stem : {"online_log_loss", "online_brier"}) {
          std::ofstream(std::string(stem) + ".svg") << "<svg></svg>";
          if (png) std::ofstream(std::string(stem) + ".png", std::ios::binary) << "\x89PNG\r\n\x1a\nfixture";
        }
      }
    } else {
      if (fs::exists(run / "ground_truth.csv") || fs::exists(run / "latent_parameters.csv"))
        throw std::runtime_error("推定器の作業先に正解情報があります。");
      const bool ranked = std::string(argv[2]) == "rankedGlicko2";
      const bool conditional = std::string(argv[2]) == "AdvantageElo" && fs::exists(run / "conditions.csv");
      if (ranked && !fs::is_regular_file(cwd / "samples/glicko_baseline/glicko2_rating.csv"))
        throw std::runtime_error("rankedGlicko2の参照分布がありません。");
      if (std::distance(fs::directory_iterator(run), fs::directory_iterator{}) != (conditional ? 4 : 3))
        throw std::runtime_error("推定器の入力は設定・ID・対戦履歴だけにしてください。");
      std::ifstream snapshot(run / "config.json");
      const std::string settings((std::istreambuf_iterator<char>(snapshot)), {});
      for (const auto* name : {"side_advantage", "condition_seed", "ability_distribution", "performance_std", "\"seeds\""})
        if (settings.find(name) != std::string::npos)
          throw std::runtime_error("推定器へ真の生成条件を渡しています。");
      auto real = fs::path(argv[0]).parent_path() / "real_project10_predict";
#ifdef _WIN32
      real += ".exe";
#endif
      project10::io::runProcess(real.string(), {argv[1], argv[2]}, cwd);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
