#include "project10/evaluation/plot.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/evaluation/reuse.hpp"
#include "project10/evaluation/reference.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/process.hpp"
#include "project10/simulation/run.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace io = project10::io;
namespace {

// この処理で排他的に作成した一時領域だけを削除する。実験結果は失敗時も残す。
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

void prepareWorker(const fs::path& directory, const fs::path& run, int runID,
                   const std::string& method, bool comparison) {
  const auto workerRun = directory / "runs" / run.filename();
  fs::create_directories(workerRun);
  // 共通の対戦履歴を複製し、正解情報は評価側に残す。
  io::writeRatingSnapshot(runID, workerRun);
  for (const auto* name : {"players.csv", "matches.csv"})
    fs::copy_file(run / name, workerRun / name);
  if (comparison && method == "AdvantageElo")
    fs::copy_file(run / "conditions.csv", workerRun / "conditions.csv");
  if (method == "rankedGlicko2") {
    const auto baseline = directory / "samples/glicko_baseline";
    fs::create_directories(baseline);
    fs::copy_file(project10::evaluation::fixedReferenceDirectory(runID) / "glicko2_rating.csv",
                  baseline / "glicko2_rating.csv");
  }
}

void publishPredictions(const fs::path& workerRun, const fs::path& run, int runID,
                        const std::string& method, bool saveFinalRatings, bool comparison) {
  const auto config = io::loadEvaluationConfig(runID, method);
  const bool finalRatings = saveFinalRatings && method == "Glicko2";
  std::vector<std::string> files{config.predictionsFile};
  if (finalRatings) files.push_back("glicko2_rating.csv");
  if (comparison && method == "AdvantageElo") files.push_back("online_predictions.csv");
  for (const auto& file : files) {
    const auto source = workerRun / file;
    if (!fs::is_regular_file(fs::symlink_status(source)) || fs::file_size(source) == 0)
      throw std::runtime_error("出力CSVがありません、または空です: " + file);
  }
  if (finalRatings)
    io::readGlicko2RatingsCsv((workerRun / "glicko2_rating.csv").string(), config.numPlayers);

  std::size_t published = 0;
  try {
    for (const auto& file : files) {
      fs::create_hard_link(workerRun / file, run / file);
      ++published;
    }
  } catch (...) {
    // 予測と付随CSVを揃えて保存し、既存のファイルには触れない。
    while (published > 0) {
      const auto destination = run / files[--published];
      std::error_code error;
      fs::remove(destination, error);
      if (error) std::cerr << "保存途中のCSVを削除できません: " << destination.string() << '\n';
    }
    throw;
  }
}
}  // namespace

int main(int argc, char** argv) {
  std::string stage = "設定確認";
  try {
    bool png = false, reuse = false, allMethods = false, comparison = false, ranking = false;
    std::size_t jobs = 1;
    std::string gnuplot = "gnuplot", name = "graphs";
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--png") png = true;
      else if (option == "--all-methods") allMethods = true;
      else if (option == "--comparison") comparison = true;
      else if (option == "--ranking") ranking = true;
      else if (option == "--ranked-reference-run")
        throw std::invalid_argument("--ranked-reference-runは使用できません。固定の参照分布を使うには、新しいrunで--all-methodsを指定してください。");
      else if (option == "--reuse-existing") reuse = true;
      else if (option == "--jobs") {
        if (++i == argc) throw std::invalid_argument("--jobsには正の整数を指定してください。");
        const std::string_view value = argv[i];
        const auto end = value.data() + value.size();
        const auto parsed = std::from_chars(value.data(), end, jobs);
        if (parsed.ec != std::errc{} || parsed.ptr != end || jobs == 0)
          throw std::invalid_argument("--jobsには正の整数を指定してください。");
      } else if (option == "--gnuplot" && i + 1 < argc) gnuplot = argv[++i];
      else if (option == "--name" && i + 1 < argc) name = argv[++i];
      else if (option == "--help") {
        std::cout << "project10_run [--all-methods] [--comparison] [--ranking] [--reuse-existing] [--jobs N] [--png] [--name graphs] [--gnuplot executable]\n"
                     "既定では4手法を実行します。--all-methodsを指定すると、固定の参照分布を使って6手法を実行します。\n"
                     "--comparisonを指定すると、先手・後手の条件付き評価を行います。6手法を評価する場合は--all-methodsも指定してください。\n"
                     "--rankingを指定すると、順位距離の評価CSVとグラフも生成します。\n"
                     "--jobs 2を指定すると、最大2手法の予測CSVを並列に生成します。既定は1です。\n";
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
    if (!reuse && fs::exists(fs::symlink_status(run))) throw std::runtime_error("既存runへの上書きはできません。新しいrun IDを指定するか、--reuse-existingで既存の成果物を再利用してください。");
    if (reuse && (fs::is_symlink(run) || !fs::is_directory(run)))
      throw std::runtime_error("再利用するrunの保存先には、シンボリックリンクではない既存のディレクトリが必要です。");
    const auto simulation = io::loadConfig(runID);
    if (simulation.conditional != comparison)
      throw std::runtime_error("comparison設定と--comparisonは両方を指定してください。");
    const auto methods = io::ratingMethods(allMethods || comparison, allMethods);
    for (const auto& method : methods) {
      const auto config = io::loadRatingConfig(runID, method, "configs");
      if (method == "Glicko2" && (config.period != 10 || config.interval != 10 ||
                                   config.matchesPerPlayer % 10 != 0))
        throw std::runtime_error("Glicko2では、更新期間と評価間隔を1人当たり10試合、1人当たりの総対戦数を10の倍数に設定してください。");
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
      std::cout << "rankedGlicko2: samples/glicko_baseline の固定参照分布を使用します。\n"
                   "評価用データとは別のデータで学習した分布を使って比較します。\n";
    fs::create_directories("runs");
    const auto lockPath = run.parent_path() / ("." + run.filename().string() + ".lock");
    const io::RunLock lock(lockPath);
    project10::evaluation::ReusePlan plan;
    if (reuse) {
      stage = "再利用検証";
      plan = project10::evaluation::inspectReusableRun(runID, lock, allMethods, comparison, -1, ranking);
      std::cout << "再利用: 生成済み共通入力\n";
    } else {
      stage = "生成";
      std::cout << stage << '\n' << std::flush;
      if (!project10::simulation::generateRun(runID, {}, &lock))
        throw std::runtime_error("既存runへの上書きはできません。新しいrun IDを指定するか、--reuse-existingで既存の成果物を再利用してください。");
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
    const auto stdinPath = workspace.path / "stdin.txt";
    writeText(stdinPath, std::to_string(runID) + "\n");
    for (std::size_t first = 0; first < methods.size();) {
      // 子の破棄時に終了を待ってから作業先を削除する。
      const Workspace batch(workspace.path / "workers");
      std::vector<std::unique_ptr<io::ChildProcess>> children(methods.size() - first);
      std::size_t started = 0;
      std::size_t next = first;
      for (; next < methods.size() && started < jobs; ++next) {
        const auto& method = methods[next];
        if (plan.predictions.count(method)) continue;
        stage = method + "予測";
        std::cout << stage << '\n' << std::flush;
        const auto directory = batch.path / method;
        prepareWorker(directory, run, runID, method, comparison);
        const bool direct = method != "Glicko" && method != "TrueSkill";
        const auto program = sibling(argv[0], direct ? "project10_predict" :
          (method == "Glicko" ? "project10_glicko" : "project10_trueskill"));
        const auto arguments = direct ? std::vector<std::string>{std::to_string(runID), method} :
          std::vector<std::string>{};
        children[next - first] = std::make_unique<io::ChildProcess>(program, arguments, directory, stdinPath);
        ++started;
      }
      for (std::size_t i = first; i < next; ++i) {
        const auto& method = methods[i];
        if (const auto& child = children[i - first]) {
          stage = method + "予測";
          child->wait();
          publishPredictions(batch.path / method / "runs" / run.filename(), run, runID, method,
                             allMethods || comparison, comparison);
        } else {
          std::cout << "再利用: " << method << "予測\n";
        }
        const auto runEvalStep = [&](bool reused, const std::string& label, const std::string& stageSuffix, auto fn) {
          if (reused) { std::cout << "再利用: " << method << ' ' << label << '\n'; return; }
          stage = method + ' ' + stageSuffix; std::cout << stage << '\n' << std::flush;
          const auto result = fn(runID, method, &lock);
          if (!result.cleanupWarning.empty())
            std::cerr << "一時ファイルを削除できません: " << result.cleanupWarning.string() << '\n';
        };
        runEvalStep(plan.mse.count(method) != 0, "MSE", "MSE評価", project10::evaluation::evaluateRun);
        if (ranking) runEvalStep(plan.ranking.count(method) != 0, "順位評価", "順位評価", project10::evaluation::evaluateRunRanking);
      }
      first = next;
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
    project10::evaluation::plotRun(runID, png, name, gnuplot, std::cout, &lock, reuse, ranking);
    std::cout << "一括実行完了: " << run.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー（" << stage << "）: " << error.what() << '\n';
    return 1;
  }
}
