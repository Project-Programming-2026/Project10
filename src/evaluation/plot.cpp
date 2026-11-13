#include "project10/evaluation/plot.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/io/process.hpp"
#include "project10/evaluation/reference.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace project10::evaluation {
namespace {
namespace fs = std::filesystem;
const auto algorithms = io::ratingMethods(true);
const char* colors[]{"#0072B2", "#D55E00", "#009E73", "#CC79A7", "#E69F00", "#56B4E9"};

void runGnuplot(const std::string& program, const fs::path& directory) {
  try {
    io::runProcess(program, {"-d", "plot.gp"}, directory);
  } catch (const std::exception&) {
    throw std::runtime_error("gnuplotの起動または描画に失敗しました。PATH・--gnuplot・"
                             "svg/pngcairo端末の対応を確認してください。");
  }
}

double yMaximum(const std::vector<MseSeries>& series) {
  double largest = 0.0;
  for (const auto& method : series)
    for (const auto& point : method.points) largest = std::max(largest, point.mse);
  // 全点が0でも有効な軸幅を確保する。正の値は10%の余白を取り、1・2・5倍へ切り上げる。
  if (largest == 0.0) return 0.01;
  const double target = std::min(1.0, largest * 1.1);
  double unit = 1.0;
  while (unit > target && unit / 10.0 > 0.0) unit /= 10.0;
  for (const double factor : {1.0, 2.0, 5.0}) {
    const double upper = std::min(1.0, unit * factor);
    if (upper >= target) return upper;
  }
  return std::min(1.0, unit * 10.0);
}

std::string makeScript(int runID, int maximum, const std::vector<MseSeries>& series, bool png,
                       const std::string& scenario, bool fixedReference,
                       const std::vector<OnlineMetrics>& metrics) {
  std::ostringstream script;
  script.imbue(std::locale::classic());
  script << std::setprecision(std::numeric_limits<double>::max_digits10);
  script << "# project10_plot: single run; validated MSE values; no aggregation\n";
  for (std::size_t i = 0; i < series.size(); ++i) {
    script << "# " << series[i].algorithm << " <- " << series[i].sourceFile << '\n';
    script << "$data" << i << " << EOD\n";
    for (const auto& point : series[i].points) {
      script << point.matchesPerPlayer << ' ' << point.mse << '\n';
    }
    script << "EOD\n";
  }
  std::size_t styles = metrics.empty() ? 4 : 5;
  bool smoke = false;
  for (const auto& method : series) {
    const auto index = std::find(algorithms.begin(), algorithms.end(), method.algorithm) - algorithms.begin();
    styles = std::max(styles, static_cast<std::size_t>(index + 1));
    smoke = smoke || (!fixedReference && scenario.empty() && method.algorithm == "rankedGlicko2");
  }
  script << "set encoding utf8\nset title 'Run " << runID << " - MSE comparison"
         << (scenario.empty() ? "" : " (" + scenario + "; conditional, both sides)")
         << (fixedReference ? " (fixed baseline; PR #28)" : "")
         << (smoke ? " (same-run final reference; smoke test only)" : "") << "'\n"
         << "set xlabel 'Cumulative matches per player'\nset ylabel 'MSE'\n"
         << "set xrange [0:" << maximum << "]\nset yrange [0:"
         << std::setprecision(6) << yMaximum(series) << "]\n"
         << std::setprecision(std::numeric_limits<double>::max_digits10)
         << "set grid xtics ytics\nset key outside right center\n";
  for (std::size_t i = 0; i < styles; ++i) {
    script << "set style line " << i + 1 << " lc rgb '" << colors[i]
           << "' dt " << i + 1 << " lw 2 pt " << i + 5 << " ps 0.8\n";
  }
  script << "set terminal svg size 1000,650 noenhanced\nset output 'mse.svg'\nplot ";
  for (std::size_t i = 0; i < series.size(); ++i) {
    std::size_t style = 0;
    while (algorithms[style] != series[i].algorithm) ++style;
    if (i != 0) script << ", ";
    const auto& method = series[i].algorithm;
    const std::string label = method == "Glicko2" ? "Glicko-2" :
      method == "AdvantageElo" ? "Advantage-Elo (" + (scenario.empty() ? std::string("S0") : scenario) + ")" :
      method == "rankedGlicko2" ? (fixedReference ? "ranked Glicko-2 (fixed baseline)" :
        smoke ? "ranked Glicko-2 (smoke only)" : "ranked Glicko-2 (independent ref.)") : method;
    script << "$data" << i << " using 1:2 with linespoints ls " << style + 1
           << " title '" << label << "'";
  }
  script << "\nunset output\n";
  if (png) {
    script << "set terminal pngcairo size 1000,650 noenhanced\nset output 'mse.png'\n"
           << "replot\nunset output\n";
  }
  if (!metrics.empty()) {
    script << "$online << EOD\n";
    for (const auto& row : metrics) script << row.matchesPerPlayer << ' ' << row.eloLogLoss << ' '
      << row.advantageLogLoss << ' ' << row.eloBrier << ' ' << row.advantageBrier << '\n';
    script << "EOD\n";
    for (int metric = 0; metric < 2; ++metric) {
      const std::string stem = metric == 0 ? "online_log_loss" : "online_brier";
      const std::string label = metric == 0 ? "Log Loss" : "Brier Score";
      script << "set title 'Run " << runID << " - " << scenario << " pre-match " << label
        << " (cumulative mean)'\nset ylabel '" << label << "'\nset yrange [0:*]\n"
        << "set terminal svg size 1000,650 noenhanced\nset output '" << stem << ".svg'\n"
        << "plot $online using 1:" << (metric == 0 ? 2 : 4)
        << " with linespoints ls 1 title 'Elo', $online using 1:" << (metric == 0 ? 3 : 5)
        << " with linespoints ls 5 title 'Advantage-Elo'\nunset output\n";
      if (png) script << "set terminal pngcairo size 1000,650 noenhanced\nset output '"
        << stem << ".png'\nreplot\nunset output\n";
    }
  }
  return script.str();
}

void validateImage(const fs::path& path, bool png) {
  if (fs::is_symlink(path) || !fs::is_regular_file(path)) {
    throw std::runtime_error("gnuplotの出力画像がありません: " + path.string());
  }
  std::ifstream file(path, std::ios::binary);
  std::string prefix(4096, '\0');
  file.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
  prefix.resize(static_cast<std::size_t>(file.gcount()));
  const bool valid = png ? prefix.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0 :
                          prefix.find("<svg") != std::string::npos;
  if (!valid) throw std::runtime_error("gnuplotの出力画像が不正です: " + path.string());
}
}  // namespace

void plotRun(int runID, bool png, const std::string& name,
             const std::string& gnuplot, std::ostream& log, const io::RunLock* borrowedLock,
             bool reuseExisting) {
  if (name.empty() || name.find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
    throw std::invalid_argument("出力名は英数字・ハイフン・アンダースコアで指定してください。");
  }
  const auto run = fs::absolute(fs::path("runs") / io::runDirectoryName(runID));
  if (fs::is_symlink(run) || !fs::is_directory(run)) {
    throw std::runtime_error("runの保存先が通常のディレクトリではありません。");
  }
  const auto lockPath = run.parent_path() / ("." + run.filename().string() + ".lock");
  if (borrowedLock) borrowedLock->requirePath(lockPath);
  const auto lock = borrowedLock ? nullptr : std::make_unique<io::RunLock>(lockPath);
  const auto config = io::loadPlotConfig(runID);
  std::vector<MseSeries> series;
  for (const auto& algorithm : algorithms) {
    const auto item = config.mseFiles.find(algorithm);
    if (item == config.mseFiles.end()) {
      log << "省略: " << algorithm << "（files.mseに未指定）\n";
      continue;
    }
    const auto path = run / item->second;
    const auto status = fs::symlink_status(path);
    if (!fs::exists(status)) {
      log << "省略: " << algorithm << "（MSEファイルなし: " << item->second << "）\n";
      continue;
    }
    if (!fs::is_regular_file(status)) {
      throw std::runtime_error("MSE入力は通常のファイルにしてください: " + path.string());
    }
    series.push_back({algorithm, item->second,
      io::readMseCsv(path.string(), runID, config.matchesPerPlayer, config.interval)});
    log << "使用: " << algorithm << " <- " << path.string() << '\n';
  }
  if (series.empty()) throw std::runtime_error("描画できるMSEファイルがありません。");
  const bool ranked = std::any_of(series.begin(), series.end(), [](const auto& value) {
      return value.algorithm == "rankedGlicko2";
    });
  const bool fixedReference = ranked && io::isFixedReferenceRun(runID);
  if (fixedReference) validateFixedReference(runID);
  else if (ranked && !config.scenario.empty())
    validateSavedReference(io::loadConfig(runID, "runs"), io::comparisonReferenceRun(runID));
  std::vector<OnlineMetrics> metrics;
  if (!config.scenario.empty()) {
    const auto path = run / "online_metrics.csv";
    if (!fs::is_regular_file(fs::symlink_status(path)))
      throw std::runtime_error("比較グラフには通常ファイルのonline_metrics.csvが必要です。");
    metrics = io::readOnlineMetricsCsv(path.string(), runID, config.matchesPerPlayer, config.interval);
  }
  const auto output = run / name;
  if (reuseExisting && fs::exists(fs::symlink_status(output))) {
    if (!fs::is_directory(fs::symlink_status(output)) ||
        !fs::is_regular_file(fs::symlink_status(output / "plot.gp")))
      throw std::runtime_error("既存グラフの保存先・描画スクリプトが不正です。--nameで別名を指定してください。");
    const bool savedPng = fs::exists(fs::symlink_status(output / "mse.png"));
    if (png && !savedPng)
      throw std::runtime_error("既存グラフにPNGがありません。--nameで別名を指定してください。");
    validateImage(output / "mse.svg", false);
    if (savedPng) validateImage(output / "mse.png", true);
    if (!metrics.empty()) for (const auto* stem : {"online_log_loss", "online_brier"}) {
      validateImage(output / (std::string(stem) + ".svg"), false);
      if (savedPng) validateImage(output / (std::string(stem) + ".png"), true);
    }
    std::ifstream file(output / "plot.gp");
    if (!file) throw std::runtime_error("既存の描画スクリプトを開けません。");
    std::string actual, line;
    while (std::getline(file, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      actual += line + '\n';
    }
    if (!file.eof() || actual != makeScript(runID, config.matchesPerPlayer, series, savedPng, config.scenario, fixedReference, metrics))
      throw std::runtime_error("既存グラフの描画データ・条件がMSEと一致しません。--nameで別名を指定してください。");
    log << "再利用: グラフ " << output.string() << '\n';
    return;
  }
  // 新規ディレクトリの作成を予約とし、既存のグラフ・ファイル・リンクを保護する。
  if (fs::exists(fs::symlink_status(output)) || !fs::create_directory(output)) {
    throw std::runtime_error("グラフ出力先がすでに存在します。--nameで別名を指定してください。");
  }
  try {
    std::ofstream script(output / "plot.gp");
    script << makeScript(runID, config.matchesPerPlayer, series, png, config.scenario, fixedReference, metrics);
    script.close();
    if (!script) throw std::runtime_error("描画スクリプトの保存に失敗しました。");
    runGnuplot(gnuplot, output);
    validateImage(output / "mse.svg", false);
    if (png) validateImage(output / "mse.png", true);
    if (!metrics.empty()) for (const auto* stem : {"online_log_loss", "online_brier"}) {
      validateImage(output / (std::string(stem) + ".svg"), false);
      if (png) validateImage(output / (std::string(stem) + ".png"), true);
    }
  } catch (...) {
    // この呼び出しで作成したものだけを削除し、無関係なファイルには触れない。
    std::error_code ignored;
    for (const auto* file : {"plot.gp", "mse.svg", "mse.png"}) fs::remove(output / file, ignored);
    if (!metrics.empty()) for (const auto* file : {"online_log_loss.svg", "online_log_loss.png",
          "online_brier.svg", "online_brier.png"}) fs::remove(output / file, ignored);
    fs::remove(output, ignored);
    throw;
  }
  log << "グラフ保存先: " << output.string() << "（plot.gpに描画データ・条件を保存）\n";
}
}  // namespace project10::evaluation
