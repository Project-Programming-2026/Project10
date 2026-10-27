#include "project10/evaluation/plot.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/io/config.hpp"
#include "project10/io/process.hpp"
#include "project10/simulation/run.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace io = project10::io;
namespace {
const std::vector<std::string> methods{"Elo", "Glicko", "Glicko2", "TrueSkill"};

// 排他的に作成した自分の一時領域だけを片付ける。実験結果は失敗時も残す。
struct Workspace {
  fs::path path;
  explicit Workspace(const fs::path& location) : path(location) {
    if (!fs::create_directory(path)) throw std::runtime_error("一時領域を作成できません。");
  }
  ~Workspace() {
    std::error_code error;
    fs::remove_all(path, error);
    if (error) std::cerr << "一時領域を削除できません: " << path.string() << '\n';
  }
};

void writeText(const fs::path& path, const std::string& contents) {
  std::ofstream file(path, std::ios::binary);
  file << contents;
  file.close();
  if (!file) throw std::runtime_error("ファイルを保存できません: " + path.string());
}

std::string sibling(const char* argv0, const std::string& name) {
  auto path = fs::path(argv0).parent_path() / name;
#ifdef _WIN32
  path += ".exe";
#endif
  return path.has_parent_path() ? fs::absolute(path).string() : path.string();
}
}  // namespace

int main(int argc, char** argv) {
  std::string stage = "設定確認";
  try {
    bool png = false;
    std::string gnuplot = "gnuplot";
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--png") png = true;
      else if (option == "--gnuplot" && i + 1 < argc) gnuplot = argv[++i];
      else if (option == "--help") {
        std::cout << "project10_run [--png] [--gnuplot executable]\n"
                     "run IDを標準入力から読み、4手法とMSE・グラフを実行します。新規run専用です。\n";
        return 0;
      } else throw std::invalid_argument("不正なオプションです: " + option);
    }
    std::cout << "Enter run ID: ";
    std::string input;
    std::getline(std::cin, input);
    std::istringstream parser(input);
    int runID;
    if (!(parser >> runID) || runID < 0 || !(parser >> std::ws).eof())
      throw std::invalid_argument("run IDが不正です。");
    const auto run = fs::absolute(fs::path("runs") / io::runDirectoryName(runID));
    if (fs::exists(fs::symlink_status(run))) throw std::runtime_error("既存runは使用しません。");
    io::loadConfig(runID);
    for (const auto& method : methods) {
      const auto config = io::loadRatingConfig(runID, method, "configs");
      if (method == "Glicko2" && (config.period != 10 || config.interval != 10 ||
                                   config.matchesPerPlayer % 10 != 0))
        throw std::runtime_error("Glicko2は期間・評価間隔10、総対戦数は10の倍数に限定しています。");
    }
    io::loadGlickoConfig(runID, "configs");
    io::loadPlotConfig(runID, "configs");
    fs::create_directories("runs");
    const auto lockPath = run.parent_path() / ("." + run.filename().string() + ".lock");
    const io::RunLock lock(lockPath);
    stage = "生成";
    std::cout << stage << '\n' << std::flush;
    if (!project10::simulation::generateRun(runID, {}, &lock))
      throw std::runtime_error("既存runは使用しません。");
    const Workspace workspace(lockPath / "predictions");
    writeText(workspace.path / "marker.json", "{\"schema_version\":1,\"producer\":\"project10_run\",\"run_id\":" +
      std::to_string(runID) + "}\n");
    fs::create_hard_link(workspace.path / "marker.json", run / "project10_run.json");
    const auto workerRun = workspace.path / "runs" / run.filename();
    fs::create_directories(workerRun);
    // 推定器の作業先には正解情報を置かず、共有入力を一度だけ複製する。
    for (const auto* name : {"config.json", "players.csv", "matches.csv"})
      fs::copy_file(run / name, workerRun / name);
    const auto stdinPath = workspace.path / "stdin.txt";
    writeText(stdinPath, std::to_string(runID) + "\n");
    for (const auto& method : methods) {
      stage = method + "予測";
      std::cout << stage << '\n' << std::flush;
      const bool direct = method == "Elo" || method == "Glicko2";
      const auto program = sibling(argv[0], direct ? "project10_predict" :
        (method == "Glicko" ? "project10_glicko" : "project10_trueskill"));
      io::runProcess(program, direct ? std::vector<std::string>{std::to_string(runID), method} :
        std::vector<std::string>{}, workspace.path, stdinPath);
      const auto config = io::loadEvaluationConfig(runID, method);
      fs::create_hard_link(workerRun / config.predictionsFile, run / config.predictionsFile);
      fs::remove(workerRun / config.predictionsFile);
      stage = method + " MSE評価";
      std::cout << stage << '\n' << std::flush;
      const auto result = project10::evaluation::evaluateRun(runID, method, &lock);
      if (!result.cleanupWarning.empty())
        std::cerr << "一時ファイルを削除できません: " << result.cleanupWarning.string() << '\n';
    }
    stage = "グラフ生成";
    std::cout << stage << '\n' << std::flush;
    project10::evaluation::plotRun(runID, png, "graphs", gnuplot, std::cout, &lock);
    std::cout << "一括実行完了: " << run.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー（" << stage << "）: " << error.what() << '\n';
    return 1;
  }
}
