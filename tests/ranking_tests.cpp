#include "project10/evaluation/files.hpp"
#include "project10/evaluation/plot.hpp"
#include "project10/evaluation/ranking.hpp"
#include "project10/evaluation/reuse.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/simulation/run.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ev = project10::evaluation;
namespace io = project10::io;
using nlohmann::json;

namespace {
void require(bool ok, const std::string& msg = "順位評価テスト失敗") { if (!ok) throw std::runtime_error(msg); }
void near(double actual, double expected) {
  if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > 1e-12)
    throw std::runtime_error("値不一致 actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
}
void checkTriple(const ev::RankingResult& r, double k, double w, double n) {
  near(r.kendallDistance, k); near(r.weightedKendallDistance, w); near(r.normalizedWeightedKendallDistance, n);
}
template <typename Action>
void rejects(Action action, const std::string& expected = "") {
  try { action(); } catch (const std::exception& e) {
    if (!expected.empty() && std::string(e.what()).find(expected) == std::string::npos)
      throw std::runtime_error("想定外エラー: " + std::string(e.what()));
    return;
  }
  throw std::runtime_error("不正入力が受理されました");
}
void writeText(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f << s; f.close(); require(static_cast<bool>(f)); }
std::string readText(const fs::path& p) { std::ifstream f(p, std::ios::binary); require(static_cast<bool>(f)); std::ostringstream b; b << f.rdbuf(); return b.str(); }
void feedOrder(ev::RankingEvaluator& ev, int runID, int cp, const std::vector<int>& order) {
  std::vector<int> rk(order.size());
  for (std::size_t i = 0; i < order.size(); ++i) rk[order[i]] = static_cast<int>(i);
  for (int a = 0; a < static_cast<int>(order.size()); ++a)
    for (int b = a + 1; b < static_cast<int>(order.size()); ++b) ev.addPrediction({runID, cp, a, b, rk[a] < rk[b] ? 0.8 : 0.2});
}

void testCoreMetricsAndValidation(const fs::path& dir) {
  ev::RankingEvaluator ev5(3, 40, 10, {5,4,3,2,1});
  const std::vector<std::vector<int>> orders{{0,1,2,3,4}, {1,0,2,3,4}, {4,1,2,3,0}, {4,3,2,1,0}};
  const double exp5[][3] = {{0,0,0}, {1,1,0.05}, {7,16,0.8}, {10,20,1}};
  for (int t = 0; t < 4; ++t) feedOrder(ev5, 3, (t + 1) * 10, orders[t]);
  const auto r5 = ev5.finish(); require(r5.size() == 4);
  for (int t = 0; t < 4; ++t) { require(r5[t].matchesPerPlayer == (t + 1) * 10); checkTriple(r5[t], exp5[t][0], exp5[t][1], exp5[t][2]); }
  ev::RankingEvaluator sc(3,10,10,{10,8,6,4,2}), sh(3,10,10,{2,1,0,-1,-2}), uni(1,10,10,{5,4,3,2,1});
  feedOrder(sc, 3, 10, orders[2]); feedOrder(sh, 3, 10, orders[2]);
  checkTriple(sc.finish()[0], 7, 32, 0.8); checkTriple(sh.finish()[0], 7, 16, 0.8);
  for (int a = 0; a < 5; ++a) for (int b = a + 1; b < 5; ++b) uni.addPrediction({1,10,a,b,0.5});
  checkTriple(uni.finish()[0], 5, 10, 0.5);
  const auto eval3 = [](std::vector<double> th, double a, double b, double c) {
    ev::RankingEvaluator e(1,10,10,std::move(th)); e.addPrediction({1,10,0,1,a}); e.addPrediction({1,10,0,2,b}); e.addPrediction({1,10,1,2,c}); return e.finish()[0];
  };
  checkTriple(eval3({2,2,0}, 0.8, 0.8, 0.8), 0.5, 0, 0); checkTriple(eval3({2,2,0}, 0.5, 0.2, 0.2), 2, 4, 1); checkTriple(eval3({3,2,1}, 0.5, 0.8, 0.8), 0.5, 0.5, 0.125);

  const std::vector<double> th6{1.5,-0.3,0.7,2.1,-1.2,0.0}; std::vector<ev::PredictionRow> rows; std::mt19937_64 rng(42);
  for (int t : {10, 20}) for (int a = 0; a < 6; ++a) for (int b = a + 1; b < 6; ++b)
    rows.push_back({5, t, a, b, std::uniform_real_distribution<double>(0.05, 0.95)(rng)});
  ev::RankingEvaluator fwd(5,20,10,th6), bwd(5,20,10,th6), shf(5,20,10,th6);
  for (const auto& r : rows) fwd.addPrediction(r);
  for (auto it = rows.rbegin(); it != rows.rend(); ++it) bwd.addPrediction(*it);
  std::shuffle(rows.begin(), rows.end(), rng); for (const auto& r : rows) shf.addPrediction(r);
  const auto fR = fwd.finish(), bR = bwd.finish(), sR = shf.finish();
  for (int i = 0; i < 2; ++i)
    require(fR[i].kendallDistance == bR[i].kendallDistance && fR[i].kendallDistance == sR[i].kendallDistance &&
            fR[i].weightedKendallDistance == bR[i].weightedKendallDistance && fR[i].weightedKendallDistance == sR[i].weightedKendallDistance &&
            fR[i].normalizedWeightedKendallDistance == bR[i].normalizedWeightedKendallDistance && fR[i].normalizedWeightedKendallDistance == sR[i].normalizedWeightedKendallDistance);

  const double qnan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
  rejects([&] { near(qnan, 0); }); rejects([&] { near(0, qnan); }); rejects([&] { near(inf, inf); });
  const std::vector<double> th4{4,3,2,1}; ev::RankingEvaluator cond(2, 10, 10, th4, true);
  cond.addConditionalPrediction({2,10,0,1,0.4}, 1); cond.addConditionalPrediction({2,10,0,1,0.1}, -1);
  for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b) if (a || b != 1) {
    cond.addConditionalPrediction({2,10,a,b,0.9}, 1); cond.addConditionalPrediction({2,10,a,b,0.7}, -1);
  }
  checkTriple(cond.finish()[0], 1, 1, 0.1);
  rejects([&] { ev::RankingEvaluator e(2,10,10,th4,true); e.addPrediction({2,10,0,1,0.5}); });
  rejects([&] { ev::RankingEvaluator e(2,10,10,th4,false); e.addConditionalPrediction({2,10,0,1,0.5}, 1); });
  rejects([&] { ev::RankingEvaluator e(2,10,10,th4,true); e.addConditionalPrediction({2,10,0,1,0.5}, 0); });
  rejects([&] { ev::RankingEvaluator e(2,10,10,th4,true); e.addConditionalPrediction({2,10,0,1,0.5}, 1); e.addConditionalPrediction({2,10,0,1,0.5}, 1); });
  rejects([&] { ev::RankingEvaluator e(2,10,10,th4,true);
    for (int a=0;a<4;++a) for (int b=a+1;b<4;++b) { e.addConditionalPrediction({2,10,a,b,0.5},1); if (a||b!=1) e.addConditionalPrediction({2,10,a,b,0.5},-1); } e.finish(); });
  rejects([] { ev::RankingEvaluator e(0,10,10,{1,1,1}); }, "全員同一");
  for (const auto& p : std::vector<std::vector<double>>{{1,qnan},{1,inf},{1}}) rejects([&] { ev::RankingEvaluator e(0,10,10,p); });
  rejects([] { ev::RankingEvaluator e(-1,10,10,{1,2}); }); rejects([] { ev::RankingEvaluator e(0,5,10,{1,2}); });
  for (const auto& bad : std::vector<ev::PredictionRow>{{1,10,0,1,0.5},{0,0,0,1,0.5},{0,15,0,1,0.5},{0,20,0,1,0.5},{0,10,-1,1,0.5},{0,10,1,1,0.5},
         {0,10,2,1,0.5},{0,10,0,4,0.5},{0,10,0,1,-0.01},{0,10,0,1,1.01},{0,10,0,1,qnan},{0,10,0,1,inf}})
    rejects([&] { ev::RankingEvaluator inv(0,10,10,th4); inv.addPrediction(bad); });

  const auto csv = dir / "r.csv", outCsv = dir / "w.csv";
  const std::string hdr = "run_id,matches_per_player,kendall_distance,weighted_kendall_distance,normalized_weighted_kendall_distance\n";
  writeText(csv, "run_id,matches_per_player,kendall_distance,weighted_kendall_distance,normalized_weighted_kendall_distance\r\n7,20,0,0,0\r\n7,10,1.5,2.25,0.125\r\n");
  const auto parsed = io::readRankingCsv(csv.string(), 7, 25, 10, 5);
  require(parsed.size() == 2 && parsed[0].matchesPerPlayer == 10); checkTriple(parsed[0], 1.5, 2.25, 0.125);
  io::writeRankingCsv(outCsv.string(), 7, parsed); rejects([&] { io::writeRankingCsv(outCsv.string(), 7, parsed); });
  require(io::readRankingCsv(outCsv.string(), 7, 25, 10, 5)[0].kendallDistance == 1.5);
  for (const auto& b : std::vector<std::string>{
         "wrong,matches_per_player,kendall_distance,weighted_kendall_distance,normalized_weighted_kendall_distance\n7,10,0,0,0\n",
         hdr+"8,10,0,0,0\n7,20,0,0,0\n", hdr+"7,15,0,0,0\n7,20,0,0,0\n", hdr+"7,10,-0.5,0,0\n7,20,0,0,0\n", hdr+"7,10,11,0,0\n7,20,0,0,0\n",
         hdr+"7,10,0,-1,0\n7,20,0,0,0\n", hdr+"7,10,0,0,1.1\n7,20,0,0,0\n", hdr+"7,10,nan,0,0\n7,20,0,0,0\n", hdr+"7,10,0,0,0\n7,10,0,0,0\n", hdr+"7,10,0,0,0\n"}) {
    writeText(csv, b); rejects([&] { io::readRankingCsv(csv.string(), 7, 25, 10, 5); });
  }

  constexpr int n = 1000; std::vector<double> thN(n); for (int i = 0; i < n; ++i) thN[i] = static_cast<double>(n - 1 - i);
  ev::RankingEvaluator ex(0,10,10,thN), rv(0,10,10,thN), ti(0,10,10,thN);
  for (int a = 0; a < n; ++a) for (int b = a + 1; b < n; ++b) { ex.addPrediction({0,10,a,b,0.75}); rv.addPrediction({0,10,a,b,0.25}); ti.addPrediction({0,10,a,b,0.5}); }
  checkTriple(ex.finish()[0], 0, 0, 0);
  near(rv.finish()[0].kendallDistance, 499500); near(rv.finish()[0].normalizedWeightedKendallDistance, 1);
  near(ti.finish()[0].kendallDistance, 249750); near(ti.finish()[0].normalizedWeightedKendallDistance, 0.5);
}

void testIntegrationServices(const fs::path& dir, const fs::path& stubPath) {
  const auto work = dir / "integration"; fs::create_directories(work / "configs/run_0000");
  const json preds{{"Elo","elo.csv"},{"Glicko","glicko.csv"},{"Glicko2","glicko2.csv"},{"TrueSkill","trueskill.csv"},{"AdvantageElo","advantage_elo.csv"}};
  const json mses{{"Elo","elo_mse.csv"},{"Glicko","glicko_mse.csv"},{"Glicko2","glicko2_mse.csv"},{"TrueSkill","trueskill_mse.csv"},{"AdvantageElo","advantage_elo_mse.csv"}};
  const json cfg{{"schema_version","1.0"},{"run_id",0},{"num_players",4},{"matches_per_player",20},
    {"evaluation_interval_matches_per_player",10},{"rating_period_matches_per_player",10},{"model","probit_1d"},
    {"ability_distribution",{{"type","normal"},{"mean",0.0},{"std",1.0}}},{"performance_std",1.0},{"matchmaking","uniform_random"},
    {"seeds",{{"ability",2026111301},{"matching",2026111302},{"result",2026111303}}},
    {"files",{{"players","players.csv"},{"matches","matches.csv"},{"latent_parameters","latent_parameters.csv"},
              {"ground_truth","ground_truth.csv"},{"predictions",preds},{"mse",mses}}}};
  writeText(work / "configs/run_0000/config.json", cfg.dump(2));
  { char a0[]="e", a1[]="--ranking", a2[]="--name", a3[]="bad", *ok[]={a0,a1}, *bad[]={a0,a1,a2,a3};
    require(ev::parseFileOptions(2, ok, false).ranking); rejects([&]{ ev::parseFileOptions(4, bad, false); }, "--input"); }
  const auto prev = fs::current_path();
  struct Restore { fs::path p; ~Restore() { std::error_code e; fs::current_path(p, e); } } restore{prev};
  fs::current_path(work);
  const auto run0 = project10::simulation::generateRun(0)->outputDir;
  io::writeEloPredictions(0, {0,1,2,3}, io::readMatchesCsv((run0/"matches.csv").string(), 4, 20, 10), 10, (run0/"elo.csv").string());
  writeText(run0/"project10_run.json", "{\"schema_version\":1,\"producer\":\"project10_run\",\"run_id\":0}\n");

  const auto evalRes = ev::evaluateRunRanking(0, "Elo");
  require(evalRes.output == run0 / "elo_ranking.csv" && evalRes.cleanupWarning.empty());
  rejects([&] { ev::evaluateRunRanking(0, "Elo"); }, "すでに存在します");
  { const io::RunLock w("runs/.run_0001.lock"); rejects([&] { ev::evaluateRunRanking(0, "Elo", &w); }, "対象runのロック"); }
  near(io::readRankingCsv(evalRes.output.string(), 0, 20, 10, 4)[0].normalizedWeightedKendallDistance, ev::calculateRunRanking(0, "Elo")[0].normalizedWeightedKendallDistance);

  std::ostringstream log; ev::plotRun(0, false, "g-rank", stubPath.string(), log, nullptr, false, true);
  require(fs::is_regular_file(run0/"g-rank/ranking_kendall.svg") && !fs::exists(run0/"g-rank/mse.svg") &&
          readText(run0/"g-rank/plot.gp").find("# ranking_definition: expected_wins_borda_half_tie") != std::string::npos);
  ev::evaluateRun(0, "Elo"); ev::plotRun(0, true, "g-both", stubPath.string(), log, nullptr, false, true);
  for (const auto* f : {"mse.svg","mse.png","ranking_kendall.svg","ranking_kendall.png","ranking_weighted_kendall.svg","ranking_weighted_kendall.png",
                        "ranking_normalized_weighted_kendall.svg","ranking_normalized_weighted_kendall.png"})
    require(fs::is_regular_file(run0 / "g-both" / f));
  ev::plotRun(0, true, "g-both", "missing-gnuplot", log, nullptr, true, true);
  rejects([&] { ev::plotRun(0, true, "g-both", "missing-gnuplot", log, nullptr, true, false); });

  const auto cmpE = ev::evaluateFiles({{"Elo",0,run0/"elo.csv"}}, "cmp-e", {"--ranking"}, true);
  require(json::parse(readText(cmpE/"manifest.json"))["series"][0]["ranking_definition"] == "expected_wins_borda_half_tie");
  const auto cmpP = ev::plotFiles({{"Elo",0,cmpE/"series_0001_ranking.csv"}}, "cmp-p", true, stubPath.string(), {"--ranking","--png"}, true);
  require(fs::is_regular_file(cmpP/"ranking_kendall.png") && json::parse(readText(cmpP/"manifest.json"))["operation"] == "plot_ranking" &&
          readText(cmpP/"plot.gp").find("# ranking_definition: expected_wins_borda_half_tie") != std::string::npos);

  { const io::RunLock lock("runs/.run_0000.lock");
    require(ev::inspectReusableRun(0, lock, false, false, -1, true).ranking.count("Elo") == 1);
    fs::remove(run0/"elo_ranking.csv"); require(ev::inspectReusableRun(0, lock, false, false, -1, true).ranking.count("Elo") == 0);
    writeText(run0/"elo_ranking.csv", "run_id,matches_per_player,kendall_distance,weighted_kendall_distance,normalized_weighted_kendall_distance\n0,10,6,10,0.5\n0,20,6,10,0.5\n");
    rejects([&] { ev::inspectReusableRun(0, lock, false, false, -1, true); }, "一致しません");
    fs::remove(run0/"elo.csv"); fs::remove(run0/"elo_mse.csv"); rejects([&] { ev::inspectReusableRun(0, lock, false, false, -1, true); }, "予測CSVがありません"); }

  fs::create_directories(work / "configs/run_0001");
  json c1 = cfg; c1["run_id"] = 1; c1["comparison"] = {{"scenario","S1"},{"side_advantage",1.0},{"condition_seed",2026111304}};
  c1["advantage_elo"] = {{"initial_advantage",0.0},{"k_advantage",1.0}}; writeText(work/"configs/run_0001/config.json", c1.dump(2));
  const auto r1 = project10::simulation::generateRun(1)->outputDir;
  auto m1 = io::readMatchesCsv((r1/"matches.csv").string(), 4, 20, 10); io::readConditionsCsv((r1/"conditions.csv").string(), m1);
  io::writeAdvantageEloPredictions(1, {0,1,2,3}, m1, 10, (r1/"advantage_elo.csv").string(), {1500,32,400,0,1}, true);
  require(io::readRankingCsv(ev::evaluateRunRanking(1, "AdvantageElo").output.string(), 1, 20, 10, 4).size() == 2);

  json c2 = cfg; c2["run_id"] = 2; c2["files"]["ranking"] = {{"Elo","elo_ranking.csv"}};
  fs::create_directories(work/"runs/run_0002"); writeText(work/"runs/run_0002/config.json", c2.dump(2));
  require(io::loadEvaluationConfig(2, "Elo").rankingFile == "elo_ranking.csv" && io::loadPlotConfig(2).rankingFiles.at("Elo") == "elo_ranking.csv");
  c2["files"]["ranking"]["Elo"] = "wrong_ranking.csv"; writeText(work/"runs/run_0002/config.json", c2.dump(2));
  rejects([&] { io::loadEvaluationConfig(2, "Elo"); }, "規定と一致していません");
  c2["files"]["ranking"]["Elo"] = "../escape.csv"; writeText(work/"runs/run_0002/config.json", c2.dump(2));
  rejects([&] { io::loadPlotConfig(2); }, "順位評価ファイル名が不正");

  json c3 = cfg; c3["run_id"] = 3; c3["num_players"] = 2; c3["matches_per_player"] = 10;
  c3["files"]["mse"] = {{"Elo","elo_mse.csv"}}; c3["files"]["ranking"] = {{"Elo","elo_ranking.csv"}};
  const auto run3 = work/"runs/run_0003"; fs::create_directories(run3); writeText(run3/"config.json", c3.dump(2));
  const std::string hdr = "run_id,matches_per_player,kendall_distance,weighted_kendall_distance,normalized_weighted_kendall_distance\n";
  int seq = 0;
  const auto plotBad = [&](const std::string& row, const std::string& err) {
    writeText(run3/"elo_ranking.csv", hdr + row); ++seq;
    rejects([&] { ev::plotRun(3, false, "b" + std::to_string(seq), stubPath.string(), log, nullptr, false, true); }, err);
    rejects([&] { ev::plotFiles({{"Elo",3,run3/"elo_ranking.csv"}}, "c" + std::to_string(seq), false, stubPath.string(), {"--ranking"}, true); }, err);
  };
  plotBad("3,10,2,2,1\n", "Kendall距離が範囲外"); plotBad("3,10,1,1.7e308,1\n", "縦軸の上限を計算できません");
  writeText(run3/"elo_ranking.csv", hdr + "3,10,1,4e307,1\n");
  ev::plotRun(3, false, "ok", stubPath.string(), log, nullptr, false, true);
  require(readText(run3/"ok/plot.gp").find("set yrange [0:5e+307]") != std::string::npos);
  require(readText(ev::plotFiles({{"Elo",3,run3/"elo_ranking.csv"}}, "cmp-ok", false, stubPath.string(), {"--ranking"}, true)/"plot.gp").find("set yrange [0:5e+307]") != std::string::npos);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 3, "作業ディレクトリと描画スタブを指定してください。");
    const auto dir = fs::path(argv[1]) / ("ranking-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    testCoreMetricsAndValidation(dir); testIntegrationServices(dir, fs::absolute(argv[2]));
    std::cout << "順位評価の単体・結合テストに成功しました。\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  return 0;
}
