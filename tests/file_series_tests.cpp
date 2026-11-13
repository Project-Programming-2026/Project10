#include "project10/evaluation/files.hpp"
#include "project10/evaluation/plot.hpp"
#include "project10/io/csv.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace project10;
using nlohmann::json;

void require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const std::string& expected = "") {
  bool rejected = false;
  try { action(); } catch (const std::exception& error) {
    require(std::string(error.what()).find(expected) != std::string::npos,
      "想定外のエラーです: " + std::string(error.what()));
    rejected = true;
  }
  require(rejected, "不正入力が受理されました。");
}
void write(const fs::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary);
  file << text;
  file.close();
  require(bool(file), "テスト用ファイルを保存できません。");
}
std::string read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  require(bool(file), "ファイルがありません: " + path.string());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
const std::string header = "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n";
const std::string conditionalHeader = "run_id,matches_per_player,player_a,player_b,z,predicted_p_a_wins\n";

// すべて独立した検証用の値。研究の実験結果として扱わない。
void fixture(int id, int players, int maximum, int interval, const std::string& scenario = "") {
  const auto run = fs::path("runs") / io::runDirectoryName(id);
  fs::create_directories(run);
  json config{{"run_id", id}, {"num_players", players}, {"matches_per_player", maximum},
    {"evaluation_interval_matches_per_player", interval}};
  if (!scenario.empty()) config["comparison"] = {{"scenario", scenario}};
  write(run / "config.json", config.dump());
  std::string ids = "player_id\n", truth = "player_a,player_b,p_a_wins\n", predictions = header;
  for (int a = 0; a < players; ++a) {
    ids += std::to_string(a) + '\n';
    for (int b = a + 1; b < players; ++b) {
      truth += std::to_string(a) + ',' + std::to_string(b) + ",0.5\n";
      for (int t = interval; t <= maximum; t += interval)
        predictions += std::to_string(id) + ',' + std::to_string(t) + ',' +
          std::to_string(a) + ',' + std::to_string(b) + ",0.5\n";
    }
  }
  write(run / "players.csv", ids);
  write(run / "pred.csv", predictions);
  if (scenario.empty()) write(run / "ground_truth.csv", truth);
  else {
    write(run / "ground_truth_positive.csv", "player_a,player_b,p_a_wins\n0,1,0.75\n");
    write(run / "ground_truth_negative.csv", "player_a,player_b,p_a_wins\n0,1,0.25\n");
  }
}

int main(int argc, char** argv) {
  try {
    require(argc == 4, "作業先・描画プログラム・modeが必要です。");
    const bool render = std::string(argv[3]) == "render";
    if (render && !fs::is_regular_file(argv[2])) {
      std::cout << "SKIP: gnuplotがないため実描画は未検証です。\n";
      return 77;
    }
    const auto previous = fs::current_path();
    const auto work = fs::path(argv[1]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(work);
    fs::current_path(work);
    fixture(1, 2, 20, 10);
    fixture(2, 4, 30, 15);
    fixture(3, 2, 20, 10, "S0");
    fixture(4, 2, 20, 10, "S1");
    const fs::path custom = "日本語 空白 '引用符' `path`.csv";
    write(custom, "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\r\n"
      "1,20,0,1,0.5\r\n1,10,0,1,0.75\r\n");
    const auto inputBefore = read(custom);
    const std::vector<evaluation::CsvSeriesInput> inputs{
      {"Elo K=16", 1, fs::absolute(custom)}, {"Elo K=32", 1, "runs/run_0001/pred.csv"},
      {"別run", 2, "runs/run_0002/pred.csv"}};
    const auto evaluated = evaluation::evaluateFiles(inputs, "evaluated", {"--name", "evaluated"});
    const auto points = io::readMseCsv((evaluated / "series_0001_mse.csv").string(), 1, 20, 10);
    require(points[0].mse == 0.0625 && points[1].mse == 0, "手計算したMSEと一致しません。");
    const auto manifest = json::parse(read(evaluated / "manifest.json"));
    require(manifest["series"].size() == 3 && manifest["series"][2]["config"]["num_players"] == 4 &&
      manifest["series"][0]["input"] == fs::absolute(custom).string() &&
      manifest["series"][0]["output"] == "series_0001_mse.csv" &&
      manifest["arguments"][1] == "evaluated", "manifestの対応が不正です。");
    std::vector<evaluation::CsvSeriesInput> mseInputs;
    for (int i = 0; i < 7; ++i) mseInputs.push_back({
      i == 0 ? "日本語 '引用符' \"二重\" \\path" : "series " + std::to_string(i),
      i == 6 ? 2 : 1, evaluated / (i == 6 ? "series_0003_mse.csv" : "series_0001_mse.csv")});
    const auto graph = evaluation::plotFiles(mseInputs, "graph", render, argv[2]);
    const auto script = read(graph / "plot.gp");
    require(script.find("set xrange [0:30]") != std::string::npos &&
      script.find("15 0\n30 0\n") != std::string::npos &&
      script.find("10 0.0625\n20 0\n") != std::string::npos &&
      script.find("$data6 using") != std::string::npos &&
      script.find("run 2") != std::string::npos &&
      script.find(custom.string()) == std::string::npos, "描画データ・凡例が不正です。");
    require(json::parse(read(graph / "manifest.json"))["operation"] == "plot", "描画manifestがありません。");
    if (render) {
      require(read(graph / "mse.png").substr(0, 8) == "\x89PNG\r\n\x1a\n", "PNGが不正です。");
      const auto svg = read(graph / "mse.svg");
      // SVGのテキスト要素では引用符の実体参照は任意。両方の合法な表現を受け付ける。
      require((svg.find("\"二重\"") != std::string::npos || svg.find("&quot;二重&quot;") != std::string::npos) &&
        svg.find("'引用符'") != std::string::npos && svg.find("\\path (run 1)") != std::string::npos &&
        svg.find("run 2") != std::string::npos,
        "引用符を含む凡例を描画できません。");
    }
    const auto original = read(evaluated / "series_0001_mse.csv");
    rejects([&] { evaluation::evaluateFiles(inputs, "evaluated"); });
    require(read(evaluated / "series_0001_mse.csv") == original, "既存出力が変更されました。");
    evaluation::plotFiles({mseInputs.front()}, "single", false, argv[2]);
    // 条件付きの5列は(.5-.75)^2と(.5-.25)^2の平均=.0625。
    // 6列は各側の正解をそのまま予測するためMSE=0。
    write("conditional.csv", conditionalHeader +
      "3,10,0,1,1,0.75\n3,10,0,1,-1,0.25\n3,20,0,1,1,0.75\n3,20,0,1,-1,0.25\n");
    const auto conditioned = evaluation::evaluateFiles({{"5列", 3, "runs/run_0003/pred.csv"},
      {"6列", 3, "conditional.csv"}, {"S1", 4, "runs/run_0004/pred.csv"}}, "conditioned");
    require(io::readMseCsv((conditioned / "series_0001_mse.csv").string(), 3, 20, 10)[0].mse == 0.0625 &&
      io::readMseCsv((conditioned / "series_0002_mse.csv").string(), 3, 20, 10)[0].mse == 0,
      "条件付きMSEが手計算と一致しません。");
    const evaluation::CsvSeriesInput conditionalMse{"S0", 3, conditioned / "series_0001_mse.csv"};
    const auto conditionalGraph = evaluation::plotFiles({conditionalMse,
      {"S1", 4, conditioned / "series_0003_mse.csv"}}, "conditional-graph", false, argv[2]);
    require(read(conditionalGraph / "plot.gp").find("S1") != std::string::npos, "シナリオがありません。");
    rejects([&] { evaluation::plotFiles({mseInputs.front(), conditionalMse}, "mixed", false, argv[2]); });
    require(!fs::exists("runs/comparisons/mixed"), "混在拒否後に出力が残っています。");
    const auto bad = [&](const std::string& text, const std::string& expected) {
      write("bad.csv", text);
      rejects([&] { evaluation::evaluateFiles({inputs.front(), {"bad", 1, "bad.csv"}}, "bad"); }, expected);
      require(!fs::exists("runs/comparisons/bad"), "後続系列の失敗後に出力が残っています。");
    };
    // 評価時点20の正常行も揃え、不正値が行不足によって偶然拒否されるテストにしない。
    const std::string tail = "1,20,0,1,0.5\n";
    for (const std::string row : {"2,10,0,1,0.5\n", "1,5,0,1,0.5\n"})
      bad(header + row + tail, "run IDまたは評価時点");
    for (const std::string row : {"1,10,0,2,0.5\n", "1,10,1,1,0.5\n"})
      bad(header + row + tail, "既知のID");
    for (const std::string value : {"NaN", "Inf", "1.1", "-0.1"})
      bad(header + "1,10,0,1," + value + '\n' + tail, "0以上1以下の有限な値");
    bad(header + "1,10,0,1,0.5\n1,10,0,1,0.5\n" + tail, "重複");
    bad(header + "1,10,0,1,0.5\n", "予測ペアが不足");
    bad("unknown\n", "未知の予測CSVヘッダー");
    bad(conditionalHeader + "1,10,0,1,1,0.5\n", "通常MSEには条件z付き");
    write("conditional.csv", conditionalHeader + "3,10,0,1,1,0.75\n");
    rejects([&] { evaluation::evaluateFiles({{"incomplete", 3, "conditional.csv"}}, "incomplete"); });
    write("bad-mse.csv", "run_id,matches_per_player,mse\n1,10,0.1\n");
    rejects([&] { evaluation::plotFiles({mseInputs.front(), {"bad", 1, "bad-mse.csv"}}, "bad-plot", false, argv[2]); });
    require(!fs::exists("runs/comparisons/bad-plot"), "描画の検証失敗後に出力が残っています。");
    for (const std::string label : {"", "bad`command`", "bad\nline"})
      rejects([&] { evaluation::evaluateFiles({{label, 1, custom}}, "bad-label"); });
    rejects([&] { evaluation::evaluateFiles({inputs.front(), inputs.front()}, "duplicate"); });
    rejects([&] { evaluation::evaluateFiles(inputs, "../escape"); });
    rejects([&] { evaluation::evaluateFiles({}, "empty"); });
    fs::create_directory("runs/comparisons/empty-dir");
    rejects([&] { evaluation::evaluateFiles(inputs, "empty-dir"); });
    std::error_code linkError;
    fs::create_symlink("missing", "runs/comparisons/link", linkError);
    if (!linkError) rejects([&] { evaluation::evaluateFiles(inputs, "link"); });
    else std::cout << "リンク作成不可のため出力リンク保護は未検証です。\n";
    {
      io::RunLock busy("runs/.run_0002.lock");
      rejects([&] { evaluation::evaluateFiles(inputs, "busy"); });
      require(!fs::exists("runs/.run_0001.lock"), "先に取得したロックが残っています。");
    }
    rejects([&] { evaluation::plotFiles(mseInputs, "missing-renderer", false, "nonexistent-project10-renderer"); });
    require(!fs::exists("runs/comparisons/missing-renderer"), "描画失敗後に出力が残っています。");
    require(read(custom) == inputBefore, "入力が変更されました。");
    for (int id = 1; id <= 4; ++id)
      require(!fs::exists(fs::path("runs") / ("." + io::runDirectoryName(id) + ".lock")), "ロックが残っています。");
    fs::current_path(previous);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
