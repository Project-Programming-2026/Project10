#include "project10/evaluation/plot.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/io/process.hpp"
#include "project10/evaluation/reference.hpp"

#include <algorithm>
#include <cmath>
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

struct RankingMetricSpec {
  const char* stem; const char* label; int column; bool capAtOne;
  double (*extract)(const RankingResult&);
};

const RankingMetricSpec rankingMetrics[] = {
  {"ranking_kendall", "Kendall distance", 2, false, [](const RankingResult& r) { return r.kendallDistance; }},
  {"ranking_weighted_kendall", "Weighted Kendall distance", 3, false, [](const RankingResult& r) { return r.weightedKendallDistance; }},
  {"ranking_normalized_weighted_kendall", "Normalized weighted Kendall distance", 4, true, [](const RankingResult& r) { return r.normalizedWeightedKendallDistance; }},
};

void runGnuplot(const std::string& program, const fs::path& directory) {
  try {
    io::runProcess(program, {"-d", "plot.gp"}, directory);
  } catch (const std::exception&) {
    throw std::runtime_error("gnuplotの起動または描画に失敗しました。PATH・--gnuplot・"
                             "svg/pngcairo端末の対応を確認してください。");
  }
}

double niceUpperBound(double largest, bool capAtOne) {
  // 全点が0でも有効な軸幅を確保する。正の値は10%の余白を取り、1・2・5倍へ切り上げる。
  if (!std::isfinite(largest) || largest < 0.0) throw std::invalid_argument("描画する縦軸の最大値が不正です。");
  if (largest == 0.0) return 0.01;
  // gnuplotの軸上限（DBL_MAX/2未満）と10%余白の乗算オーバーフローを防ぐ。
  if (!capAtOne && (largest > 5e307 / 1.1 || largest * 1.1 > 5e307))
    throw std::invalid_argument("描画する値が大きすぎるため縦軸の上限を計算できません。");
  const double target = capAtOne ? std::min(1.0, largest * 1.1) : largest * 1.1;
  double unit = 1.0;
  if (unit < target) while (unit <= target / 10.0) unit *= 10.0;
  else while (unit > target && unit / 10.0 > 0.0) unit /= 10.0;
  for (const double factor : {1.0, 2.0, 5.0}) {
    const double upper = capAtOne ? std::min(1.0, unit * factor) : unit * factor;
    if (upper >= target) return upper;
  }
  return capAtOne ? std::min(1.0, unit * 10.0) : unit * 10.0;
}

double yMaximum(const std::vector<MseSeries>& series) {
  double largest = 0.0;
  for (const auto& method : series)
    for (const auto& point : method.points) largest = std::max(largest, point.mse);
  return niceUpperBound(largest, true);
}

std::string runAlgorithmLabel(const std::string& method, const std::string& scenario, bool fixedRef, bool smoke) {
  if (method == "Glicko2") return "Glicko-2";
  if (method == "AdvantageElo") return "Advantage-Elo (" + (scenario.empty() ? std::string("S0") : scenario) + ")";
  if (method == "rankedGlicko2")
    return fixedRef ? "ranked Glicko-2 (fixed baseline)" : smoke ? "ranked Glicko-2 (smoke only)" : "ranked Glicko-2 (independent ref.)";
  return method;
}

template <typename FormatTitle>
void appendRankingSection(std::ostringstream& script, int maximum, const std::vector<RankingSeries>& series,
                          bool png, bool includeSource, const std::string& prefix, const std::string& suffix,
                          FormatTitle formatTitle) {
  script << "# ranking_definition: " << kRankingDefinition << '\n';
  for (std::size_t i = 0; i < series.size(); ++i) {
    if (includeSource) script << "# " << series[i].algorithm << " (ranking) <- " << series[i].sourceFile << '\n';
    script << "$rank" << i << " << EOD\n";
    for (const auto& p : series[i].points)
      script << p.matchesPerPlayer << ' ' << p.kendallDistance << ' ' << p.weightedKendallDistance << ' ' << p.normalizedWeightedKendallDistance << '\n';
    script << "EOD\n";
  }
  for (const auto& m : rankingMetrics) {
    double largest = 0.0;
    for (const auto& method : series) for (const auto& p : method.points) largest = std::max(largest, m.extract(p));
    script << "set title '" << prefix << m.label << " comparison" << suffix << "'\nset xlabel 'Cumulative matches per player'\nset ylabel '" << m.label << "'\n"
           << "set xrange [0:" << maximum << "]\nset yrange [0:" << std::setprecision(6) << niceUpperBound(largest, m.capAtOne) << "]\n"
           << std::setprecision(std::numeric_limits<double>::max_digits10) << "set grid xtics ytics\nset key outside right center\n"
           << "set terminal svg size 1000,650 noenhanced\nset output '" << m.stem << ".svg'\nplot ";
    for (std::size_t i = 0; i < series.size(); ++i) { if (i != 0) script << ", "; formatTitle(script, i, series[i], m.column); }
    script << "\nunset output\n";
    if (png) script << "set terminal pngcairo size 1000,650 noenhanced\nset output '" << m.stem << ".png'\nreplot\nunset output\n";
  }
}

void appendRunRankingSection(std::ostringstream& script, int runID, int maximum, const std::vector<RankingSeries>& rankingSeries,
                             bool png, const std::string& scenario, bool fixedReference, bool smoke) {
  const std::string suffix = (scenario.empty() ? "" : " (" + scenario + ")") +
    std::string(fixedReference ? " (fixed baseline)" : "") + std::string(smoke ? " (same-run final reference; smoke test only)" : "");
  appendRankingSection(script, maximum, rankingSeries, png, true, "Run " + std::to_string(runID) + " - ", suffix,
    [&](std::ostringstream& out, std::size_t i, const RankingSeries& item, int col) {
      std::size_t style = 0; while (algorithms[style] != item.algorithm) ++style;
      out << "$rank" << i << " using 1:" << col << " with linespoints ls " << style + 1
          << " title '" << runAlgorithmLabel(item.algorithm, scenario, fixedReference, smoke) << "'";
    });
}

std::string makeScript(int runID, int maximum, const std::vector<MseSeries>& series, bool png,
                       const std::string& scenario, bool fixedReference,
                       const std::vector<OnlineMetrics>& metrics,
                       const std::vector<RankingSeries>& rankingSeries = {}) {
  std::ostringstream script;
  script.imbue(std::locale::classic());
  script << std::setprecision(std::numeric_limits<double>::max_digits10);
  script << (series.empty() ? "# project10_plot: single run; validated ranking values; no aggregation\n" :
                              "# project10_plot: single run; validated MSE values; no aggregation\n");
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
  const auto scanMethod = [&](const auto& method) {
    const auto index = std::find(algorithms.begin(), algorithms.end(), method.algorithm) - algorithms.begin();
    styles = std::max(styles, static_cast<std::size_t>(index + 1));
    smoke = smoke || (!fixedReference && scenario.empty() && method.algorithm == "rankedGlicko2");
  };
  for (const auto& method : series) scanMethod(method);
  for (const auto& method : rankingSeries) scanMethod(method);
  if (series.empty()) {
    script << "set encoding utf8\n";
    for (std::size_t i = 0; i < styles; ++i)
      script << "set style line " << i + 1 << " lc rgb '" << colors[i] << "' dt " << i + 1 << " lw 2 pt " << i + 5 << " ps 0.8\n";
    appendRunRankingSection(script, runID, maximum, rankingSeries, png, scenario, fixedReference, smoke);
    return script.str();
  }
  script << "set encoding utf8\nset title 'Run " << runID << " - MSE comparison"
         << (scenario.empty() ? "" : " (" + scenario + "; conditional, both sides)")
         << (fixedReference ? " (fixed baseline)" : "")
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
    const std::string label = runAlgorithmLabel(series[i].algorithm, scenario, fixedReference, smoke);
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
  if (!rankingSeries.empty()) {
    appendRunRankingSection(script, runID, maximum, rankingSeries, png, scenario, fixedReference, smoke);
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

std::vector<std::string> plotOutputFiles(bool mse, bool online, bool ranking, bool png) {
  std::vector<std::string> stems, files;
  if (mse) stems.push_back("mse");
  if (online) stems.insert(stems.end(), {"online_log_loss", "online_brier"});
  if (ranking) for (const auto& m : rankingMetrics) stems.push_back(m.stem);
  for (const auto& stem : stems) { files.push_back(stem + ".svg"); if (png) files.push_back(stem + ".png"); }
  return files;
}
void validateAllImages(const fs::path& output, bool mse, bool online, bool ranking, bool png) {
  for (const auto& file : plotOutputFiles(mse, online, ranking, png))
    validateImage(output / file, file.size() >= 4 && file.substr(file.size() - 4) == ".png");
}
void cleanupPlotOutputs(const fs::path& output, bool mse, bool online, bool ranking) {
  std::error_code ignored; fs::remove(output / "plot.gp", ignored);
  for (const auto& file : plotOutputFiles(mse, online, ranking, true)) fs::remove(output / file, ignored);
  fs::remove(output, ignored);
}

// 呼び出し側で制御文字・バッククォートを拒否済み。二重引用符内の文字列としてのみ使う。
std::string plotLabel(const std::string& label) {
  std::string result = "\"";
  for (const char c : label) {
    if (c == '\\' || c == '"') result += '\\';
    result += c;
  }
  return result + '"';
}

fs::path finishComparisonPlot(const FileComparison& comparison, const std::string& name, const std::string& scriptText,
                              bool ranking, bool png, const std::string& gnuplot, const std::vector<std::string>& args) {
  std::vector<std::string> files{"plot.gp", "manifest.json"};
  for (const auto& img : plotOutputFiles(!ranking, false, ranking, png)) files.push_back(img);
  ComparisonOutput output(name, files);
  std::ofstream file(output.path / "plot.gp"); file << scriptText; file.close();
  if (!file) throw std::runtime_error("描画スクリプトを保存できません。");
  runGnuplot(gnuplot, output.path);
  validateAllImages(output.path, !ranking, false, ranking, png);
  comparison.writeManifest(output.path, ranking ? "plot_ranking" : "plot", args, files);
  output.complete();
  return output.path;
}
}  // namespace

fs::path plotFiles(const std::vector<CsvSeriesInput>& inputs, const std::string& name,
                   bool png, const std::string& gnuplot, const std::vector<std::string>& arguments,
                   bool ranking) {
  FileComparison comparison(inputs, name);
  std::ostringstream script;
  script.imbue(std::locale::classic());
  script << std::setprecision(std::numeric_limits<double>::max_digits10);
  int maximum = 0;
  if (ranking) {
    std::vector<RankingSeries> series;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const auto& c = comparison.contexts[i];
      const auto label = inputs[i].label + " (run " + std::to_string(inputs[i].runID) +
        (c.conditional ? "; " + c.scenario : "") + ")";
      series.push_back({label, {}, io::readRankingCsv(inputs[i].path.string(), inputs[i].runID, c.matchesPerPlayer, c.interval, c.numPlayers)});
      maximum = std::max(maximum, c.matchesPerPlayer);
    }
    script << "set encoding utf8\n";
    for (std::size_t i = 0; i < series.size(); ++i)
      script << "set style line " << i + 1 << " lc rgb '" << colors[i % 6] << "' dt " << i % 5 + 1 << " lw 2 pt " << i % 13 + 1 << " ps 0.8\n";
    appendRankingSection(script, maximum, series, png, false, "", "",
      [](std::ostringstream& out, std::size_t i, const RankingSeries& item, int col) {
        out << "$rank" << i << " using 1:" << col << " with linespoints ls " << i + 1 << " title " << plotLabel(item.algorithm);
      });
    return finishComparisonPlot(comparison, name, script.str(), true, png, gnuplot, arguments);
  }
  std::vector<MseSeries> series;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const auto& context = comparison.contexts[i];
    if (context.conditional != comparison.contexts.front().conditional)
      throw std::invalid_argument("通常MSEと条件付きMSEは同じ図に描画できません。");
    const auto label = inputs[i].label + " (run " + std::to_string(inputs[i].runID) +
      (context.conditional ? "; " + context.scenario : "") + ")";
    series.push_back({label, {}, io::readMseCsv(inputs[i].path.string(), inputs[i].runID,
      context.matchesPerPlayer, context.interval)});
    maximum = std::max(maximum, context.matchesPerPlayer);
  }
  for (std::size_t i = 0; i < series.size(); ++i) {
    script << "$data" << i << " << EOD\n";
    for (const auto& point : series[i].points) script << point.matchesPerPlayer << ' ' << point.mse << '\n';
    script << "EOD\n";
  }
  script << "set encoding utf8\nset title 'MSE comparison ("
    << (comparison.contexts.front().conditional ? "conditional, both sides" : "unconditional")
    << ")'\nset xlabel 'Cumulative matches per player'\nset ylabel 'MSE'\n"
    << "set xrange [0:" << maximum << "]\nset yrange [0:" << yMaximum(series) << "]\n"
    << "set grid xtics ytics\nset key outside right center\n";
  for (std::size_t i = 0; i < series.size(); ++i)
    script << "set style line " << i + 1 << " lc rgb '" << colors[i % 6]
      << "' dt " << i % 5 + 1 << " lw 2 pt " << i % 13 + 1 << " ps 0.8\n";
  script << "set terminal svg size 1000,650 noenhanced\nset output 'mse.svg'\nplot ";
  for (std::size_t i = 0; i < series.size(); ++i) {
    if (i) script << ", ";
    script << "$data" << i << " using 1:2 with linespoints ls " << i + 1
      << " title " << plotLabel(series[i].algorithm);
  }
  script << "\nunset output\n";
  if (png) script << "set terminal pngcairo size 1000,650 noenhanced\nset output 'mse.png'\nreplot\nunset output\n";
  return finishComparisonPlot(comparison, name, script.str(), false, png, gnuplot, arguments);
}

void plotRun(int runID, bool png, const std::string& name,
             const std::string& gnuplot, std::ostream& log, const io::RunLock* borrowedLock,
             bool reuseExisting, bool ranking) {
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
  if (!ranking && series.empty()) throw std::runtime_error("描画できるMSEファイルがありません。");
  std::vector<RankingSeries> rankingSeries;
  if (ranking) {
    for (const auto& algorithm : algorithms) {
      const auto item = config.rankingFiles.find(algorithm);
      if (item == config.rankingFiles.end()) { log << "省略(順位): " << algorithm << "（files.rankingに未指定）\n"; continue; }
      const auto path = run / item->second;
      const auto status = fs::symlink_status(path);
      if (!fs::exists(status)) { log << "省略(順位): " << algorithm << "（順位評価ファイルなし: " << item->second << "）\n"; continue; }
      if (!fs::is_regular_file(status)) throw std::runtime_error("順位評価入力は通常のファイルにしてください: " + path.string());
      rankingSeries.push_back({algorithm, item->second,
        io::readRankingCsv(path.string(), runID, config.matchesPerPlayer, config.interval, config.numPlayers)});
      log << "使用(順位): " << algorithm << " <- " << path.string() << '\n';
    }
    if (rankingSeries.empty()) throw std::runtime_error("描画できる順位評価ファイルがありません。");
  }
  const auto isRanked = [](const auto& items) {
    return std::any_of(items.begin(), items.end(), [](const auto& v) { return v.algorithm == "rankedGlicko2"; });
  };
  const bool ranked = isRanked(series) || isRanked(rankingSeries);
  const bool fixedReference = ranked && io::isFixedReferenceRun(runID);
  if (fixedReference) validateFixedReference(runID);
  else if (ranked && !config.scenario.empty())
    validateSavedReference(io::loadConfig(runID, "runs"), io::comparisonReferenceRun(runID));
  std::vector<OnlineMetrics> metrics;
  if (!config.scenario.empty() && !series.empty()) {
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
    const bool savedPng = fs::exists(fs::symlink_status(
      output / (!series.empty() ? "mse.png" : "ranking_kendall.png")));
    if (png && !savedPng)
      throw std::runtime_error("既存グラフにPNGがありません。--nameで別名を指定してください。");
    validateAllImages(output, !series.empty(), !metrics.empty(), ranking, savedPng);
    std::ifstream file(output / "plot.gp");
    if (!file) throw std::runtime_error("既存の描画スクリプトを開けません。");
    std::string actual, line;
    while (std::getline(file, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      actual += line + '\n';
    }
    if (!file.eof() || actual != makeScript(runID, config.matchesPerPlayer, series, savedPng,
          config.scenario, fixedReference, metrics, rankingSeries))
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
    script << makeScript(runID, config.matchesPerPlayer, series, png,
      config.scenario, fixedReference, metrics, rankingSeries);
    script.close();
    if (!script) throw std::runtime_error("描画スクリプトの保存に失敗しました。");
    runGnuplot(gnuplot, output);
    validateAllImages(output, !series.empty(), !metrics.empty(), ranking, png);
  } catch (...) {
    // この呼び出しで作成したものだけを削除し、無関係なファイルには触れない。
    cleanupPlotOutputs(output, !series.empty(), !metrics.empty(), ranking);
    throw;
  }
  log << "グラフ保存先: " << output.string() << "（plot.gpに描画データ・条件を保存）\n";
}
}  // namespace project10::evaluation
