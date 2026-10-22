#include "project10/evaluation/plot.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace project10::evaluation {
namespace {
namespace fs = std::filesystem;
const std::vector<std::string> algorithms{"Elo", "Glicko", "Glicko2", "TrueSkill"};
const char* colors[]{"#0072B2", "#D55E00", "#009E73", "#CC79A7"};

void runGnuplot(const std::string& program, const fs::path& directory) {
  // シェルを通さず引数を渡す。パスはスクリプトへ埋め込まない。
  auto executable = fs::path(program);
  if (executable.has_parent_path()) executable = fs::absolute(executable);
#ifdef _WIN32
  const auto native = executable.wstring();
  if (native.find(L'"') != std::wstring::npos) {
    throw std::invalid_argument("gnuplotのパスに引用符を含めないでください。");
  }
  std::wstring command = L"\"" + native + L"\" -d plot.gp";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, directory.c_str(), &startup, &process)) {
    throw std::runtime_error("gnuplotを起動できません。PATHまたは--gnuplotを確認してください（コード: " +
                             std::to_string(GetLastError()) + "）。");
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD status = 1;
  const bool obtained = GetExitCodeProcess(process.hProcess, &status) != 0;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (!obtained || status != 0) {
#else
  const auto child = fork();
  if (child < 0) throw std::runtime_error("gnuplotのプロセスを作成できません。");
  if (child == 0) {
    if (chdir(directory.c_str()) != 0) _exit(126);
    execlp(executable.c_str(), executable.c_str(), "-d", "plot.gp", static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  pid_t result;
  do { result = waitpid(child, &status, 0); } while (result == -1 && errno == EINTR);
  if (result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
#endif
    throw std::runtime_error("gnuplotの起動または描画に失敗しました。PATH・--gnuplot・"
                             "svg/pngcairo端末の対応を確認してください。");
  }
}

void writeScript(const fs::path& path, int runID, int maximum,
                 const std::vector<MseSeries>& series, bool png) {
  std::ofstream script(path);
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
  script << "set encoding utf8\nset title 'Run " << runID << " - MSE comparison'\n"
         << "set xlabel 'Cumulative matches per player'\nset ylabel 'MSE'\n"
         << "set xrange [0:" << maximum << "]\nset yrange [0:1]\n"
         << "set grid xtics ytics\nset key outside right center\n";
  for (std::size_t i = 0; i < algorithms.size(); ++i) {
    script << "set style line " << i + 1 << " lc rgb '" << colors[i]
           << "' dt " << i + 1 << " lw 2 pt " << i + 5 << " ps 0.8\n";
  }
  script << "set terminal svg size 1000,650 noenhanced\nset output 'mse.svg'\nplot ";
  for (std::size_t i = 0; i < series.size(); ++i) {
    std::size_t style = 0;
    while (algorithms[style] != series[i].algorithm) ++style;
    if (i != 0) script << ", ";
    script << "$data" << i << " using 1:2 with linespoints ls " << style + 1
           << " title '" << (series[i].algorithm == "Glicko2" ? "Glicko-2" : series[i].algorithm) << "'";
  }
  script << "\nunset output\n";
  if (png) {
    script << "set terminal pngcairo size 1000,650 noenhanced\nset output 'mse.png'\n"
           << "replot\nunset output\n";
  }
  script.close();
  if (!script) throw std::runtime_error("描画スクリプトの保存に失敗しました。");
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
             const std::string& gnuplot, std::ostream& log) {
  if (name.empty() || name.find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
    throw std::invalid_argument("出力名は英数字・ハイフン・アンダースコアで指定してください。");
  }
  const auto run = fs::absolute(fs::path("runs") / io::runDirectoryName(runID));
  if (fs::is_symlink(run) || !fs::is_directory(run)) {
    throw std::runtime_error("runの保存先が通常のディレクトリではありません。");
  }
  const io::RunLock lock(run.parent_path() / ("." + run.filename().string() + ".lock"));
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
  const auto output = run / name;
  // 新規ディレクトリの作成を予約とし、既存のグラフ・ファイル・リンクを保護する。
  if (fs::exists(fs::symlink_status(output)) || !fs::create_directory(output)) {
    throw std::runtime_error("グラフ出力先がすでに存在します。--nameで別名を指定してください。");
  }
  try {
    writeScript(output / "plot.gp", runID, config.matchesPerPlayer, series, png);
    runGnuplot(gnuplot, output);
    validateImage(output / "mse.svg", false);
    if (png) validateImage(output / "mse.png", true);
  } catch (...) {
    // この呼び出しで作成したものだけを削除し、無関係なファイルには触れない。
    std::error_code ignored;
    for (const auto* file : {"plot.gp", "mse.svg", "mse.png"}) fs::remove(output / file, ignored);
    fs::remove(output, ignored);
    throw;
  }
  log << "グラフ保存先: " << output.string() << "（plot.gpに描画データ・条件を保存）\n";
}
}  // namespace project10::evaluation
