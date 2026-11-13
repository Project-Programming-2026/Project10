#include "project10/evaluation/plot.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/evaluation/reuse.hpp"
#include "project10/evaluation/reference.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/process.hpp"
#include "project10/simulation/run.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace io = project10::io;
namespace {

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
    bool png = false, reuse = false, allMethods = false, comparison = false;
    std::string gnuplot = "gnuplot", name = "graphs";
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--png") png = true;
      else if (option == "--all-methods") allMethods = true;
      else if (option == "--comparison") comparison = true;
      else if (option == "--ranked-reference-run")
        throw std::invalid_argument("PR #28以降は固定参照を使用します。新しいrunで--all-methodsを指定してください。");
      else if (option == "--reuse-existing") reuse = true;
      else if (option == "--gnuplot" && i + 1 < argc) gnuplot = argv[++i];
      else if (option == "--name" && i + 1 < argc) name = argv[++i];
      else if (option == "--help") {
        std::cout << "project10_run [--all-methods] [--reuse-existing] [--png] [--name graphs] [--gnuplot executable]\n"
                     "通常は4手法。--all-methodsはPR #28の固定参照で6手法を実行します。\n"
                     "--comparisonは既存の条件付き評価。6手法には--all-methodsも指定します。\n";
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
    if (!reuse && fs::exists(fs::symlink_status(run))) throw std::runtime_error("既存runは使用しません。");
    if (reuse && (fs::is_symlink(run) || !fs::is_directory(run)))
      throw std::runtime_error("再利用には既存の通常runが必要です。");
    const auto simulation = io::loadConfig(runID);
    if (simulation.conditional != comparison)
      throw std::runtime_error("comparison設定と--comparisonは両方を指定してください。");
    const auto methods = io::ratingMethods(allMethods || comparison, allMethods);
    for (const auto& method : methods) {
      const auto config = io::loadRatingConfig(runID, method, "configs");
      if (method == "Glicko2" && (config.period != 10 || config.interval != 10 ||
                                   config.matchesPerPlayer % 10 != 0))
        throw std::runtime_error("Glicko2は期間・評価間隔10、総対戦数は10の倍数に限定しています。");
    }
    io::loadGlickoConfig(runID, "configs");
    io::loadPlotConfig(runID, "configs");
    if (allMethods || comparison) io::loadAdvantageParameters(runID, "configs");
    project10::evaluation::ReferenceData reference;
    if (allMethods && !reuse)
      reference = project10::evaluation::captureFixedReference(simulation);
    if (comparison)
      std::cout << "比較: " << simulation.scenario << "、同一履歴・条件付きMSE、"
                   "Elo/Advantage-Eloの試合前Log Loss・Brier Score\n";
    if (allMethods)
      std::cout << "rankedGlicko2: PR #28の固定参照 samples/glicko_baseline を使用します。\n"
                   "独立した追加学習を含む比較です。\n";
    fs::create_directories("runs");
    const auto lockPath = run.parent_path() / ("." + run.filename().string() + ".lock");
    const io::RunLock lock(lockPath);
    project10::evaluation::ReusePlan plan;
    if (reuse) {
      stage = "再利用検証";
      plan = project10::evaluation::inspectReusableRun(runID, lock, allMethods, comparison);
      std::cout << "再利用: 生成済み共通入力\n";
    } else {
      stage = "生成";
      std::cout << stage << '\n' << std::flush;
      if (!project10::simulation::generateRun(runID, {}, &lock))
        throw std::runtime_error("既存runは使用しません。");
    }
    const Workspace workspace(lockPath / "predictions");
    if (!reuse) {
      const std::string mode = allMethods ? (comparison ? "conditional_fixed_reference_v1" :
        "six_method_fixed_reference_v1") : comparison ? "conditional_comparison_v1" : "";
      writeText(workspace.path / "marker.json", "{\"schema_version\":1,\"producer\":\"project10_run\",\"run_id\":" +
        std::to_string(runID) + (mode.empty() ? "}\n" : ",\"mode\":\"" + mode + "\"" +
          (comparison && !allMethods ? ",\"ranked_reference_run\":-1}\n" : "}\n")));
      fs::create_hard_link(workspace.path / "marker.json", run / "project10_run.json");
      if (allMethods) {
        const auto saved = project10::evaluation::fixedReferenceDirectory(runID);
        fs::create_directories(saved);
        writeText(workspace.path / "reference-config.json", reference.config);
        writeText(workspace.path / "reference-ratings.csv", reference.ratings);
        fs::create_hard_link(workspace.path / "reference-config.json", saved / "config.json");
        fs::create_hard_link(workspace.path / "reference-ratings.csv", saved / "glicko2_rating.csv");
      }
    }
    const auto workerRun = workspace.path / "runs" / run.filename();
    fs::create_directories(workerRun);
    // 推定器の作業先には正解情報を置かず、共有入力を一度だけ複製する。
    io::writeRatingSnapshot(runID, workerRun);
    for (const auto* name : {"players.csv", "matches.csv"})
      fs::copy_file(run / name, workerRun / name);
    const auto stdinPath = workspace.path / "stdin.txt";
    writeText(stdinPath, std::to_string(runID) + "\n");
    for (const auto& method : methods) {
      if (plan.predictions.count(method)) {
        std::cout << "再利用: " << method << "予測\n";
      } else {
        stage = method + "予測";
        std::cout << stage << '\n' << std::flush;
        const bool direct = method != "Glicko" && method != "TrueSkill";
        if (method == "rankedGlicko2") {
          // PR #28の相対パスを隔離作業先で再現する。数式・手法側のパスは変更しない。
          const auto baseline = workspace.path / "samples/glicko_baseline";
          fs::create_directories(baseline);
          fs::copy_file(project10::evaluation::fixedReferenceDirectory(runID) / "glicko2_rating.csv",
            baseline / "glicko2_rating.csv");
        }
        if (comparison && method == "AdvantageElo")
          fs::copy_file(run / "conditions.csv", workerRun / "conditions.csv");
        const auto program = sibling(argv[0], direct ? "project10_predict" :
          (method == "Glicko" ? "project10_glicko" : "project10_trueskill"));
        io::runProcess(program, direct ? std::vector<std::string>{std::to_string(runID), method} :
          std::vector<std::string>{}, workspace.path, stdinPath);
        const auto config = io::loadEvaluationConfig(runID, method);
        if ((allMethods || comparison) && method == "Glicko2") {
          const auto ratings = workerRun / "glicko2_rating.csv";
          if (!fs::is_regular_file(fs::symlink_status(ratings)))
            throw std::runtime_error("Glicko2の最終レートCSVがありません。");
          io::readGlicko2RatingsCsv(ratings.string(), config.numPlayers);
          fs::create_hard_link(ratings, run / "glicko2_rating.csv");
          fs::remove(ratings);
        }
        fs::create_hard_link(workerRun / config.predictionsFile, run / config.predictionsFile);
        fs::remove(workerRun / config.predictionsFile);
        if (method == "rankedGlicko2") fs::remove(workspace.path / "samples/glicko_baseline/glicko2_rating.csv");
        if (comparison && method == "AdvantageElo") {
          fs::create_hard_link(workerRun / "online_predictions.csv", run / "online_predictions.csv");
          fs::remove(workerRun / "online_predictions.csv");
          fs::remove(workerRun / "conditions.csv");
        }
      }
      if (plan.mse.count(method)) {
        std::cout << "再利用: " << method << " MSE\n";
        continue;
      }
      stage = method + " MSE評価";
      std::cout << stage << '\n' << std::flush;
      const auto result = project10::evaluation::evaluateRun(runID, method, &lock);
      if (!result.cleanupWarning.empty())
        std::cerr << "一時ファイルを削除できません: " << result.cleanupWarning.string() << '\n';
    }
    if (comparison && !plan.onlineMetrics) {
      stage = "試合前評価";
      auto matches = io::readMatchesCsv((run / "matches.csv").string(), simulation.numPlayers,
        simulation.matchesPerPlayer, simulation.ratingPeriodMatchesPerPlayer);
      io::readConditionsCsv((run / "conditions.csv").string(), matches);
      const auto config = io::loadRatingConfig(runID, "Elo");
      const auto metrics = project10::evaluation::calculateOnlineMetrics(
        (run / "online_predictions.csv").string(), runID, matches, config.numPlayers, config.interval);
      io::writeOnlineMetricsCsv((workspace.path / "online_metrics.csv").string(), runID, metrics);
      fs::create_hard_link(workspace.path / "online_metrics.csv", run / "online_metrics.csv");
    }
    stage = "グラフ生成";
    std::cout << stage << '\n' << std::flush;
    project10::evaluation::plotRun(runID, png, name, gnuplot, std::cout, &lock, reuse);
    std::cout << "一括実行完了: " << run.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー（" << stage << "）: " << error.what() << '\n';
    return 1;
  }
}
