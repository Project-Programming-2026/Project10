// テスト専用: 推定器へ正解情報を渡さず、親のrunロックを全工程で保持することを観測する。
#include "project10/io/process.hpp"
#include "project10/io/run_lock.hpp"
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
int main(int argc, char** argv) {
  try {
    if (argc != 3) return 1;
    const bool render = std::string(argv[1]) == "-d";
    const auto cwd = fs::current_path();
    const auto run = render ? cwd.parent_path() : cwd / "runs" / "run_0000";
    const auto lock = render ? run.parent_path() / ("." + run.filename().string() + ".lock") : cwd.parent_path();
    bool busy = false;
    try { const project10::io::RunLock other(lock); }
    catch (const std::exception&) { busy = true; }
    if (!busy) throw std::runtime_error("親のrunロックがありません。");
    if (render) {
      std::ofstream("mse.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
      std::ofstream("mse.png", std::ios::binary) << "\x89PNG\r\n\x1a\nfixture";
    } else {
      if (fs::exists(run / "ground_truth.csv") || fs::exists(run / "latent_parameters.csv"))
        throw std::runtime_error("推定器の作業先に正解情報があります。");
      if (std::distance(fs::directory_iterator(run), fs::directory_iterator{}) != 3)
        throw std::runtime_error("推定器の入力は設定・ID・対戦履歴だけにしてください。");
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
