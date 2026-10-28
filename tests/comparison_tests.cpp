#include "project10/io/csv.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/simulation/probit.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace io = project10::io;
namespace ev = project10::evaluation;
namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void near(double a, double b) { require(std::abs(a - b) < 1e-12, "数値照合の失敗"); }
template<class F> void rejects(F action) {
  bool failed = false;
  try { action(); } catch (const std::exception&) { failed = true; }
  require(failed, "不正入力を受理しました");
}
void write(const fs::path& path, const std::string& text) { std::ofstream(path) << text; }
}
int main(int argc, char** argv) {
  try {
    require(argc == 2, "作業先が必要です");
    const fs::path root(argv[1]);
    fs::create_directories(root);
    // Phi(1/sqrt(2))。側を反転してプレイヤーを交換すると補数になる。
    near(calculateConditionalWinProbability(0, 0, 1, 1, 1), 0.7602499389065233);
    near(calculateConditionalWinProbability(2, -1, 1, 1, 1) +
         calculateConditionalWinProbability(-1, 2, 1, 1, -1), 1);
    require(calculateConditionalWinProbability(1, 2, 1, 0, -1) ==
            calculateWinProbability(1, 2, 1), "S0の元の計算が変わりました");
    rejects([] { calculateConditionalWinProbability(0, 0, 1, -1, 1); });
    rejects([] { calculateConditionalWinProbability(0, 0, 1, 1, 2); });
    rejects([] { calculateConditionalWinProbability(0, 0, 1,
      std::numeric_limits<double>::infinity(), 1); });
    const std::vector<double> abilities{0, 1, 2, 3};
    const auto old = generateMatches(abilities, 20, 10, 1, 2, 3);
    const auto zero = generateMatches(abilities, 20, 10, 1, 2, 3, 0, 4, true);
    const auto other = generateMatches(abilities, 20, 10, 1, 2, 3, 1, 5, true);
    const auto repeated = generateMatches(abilities, 20, 10, 1, 2, 3, 1, 5, true);
    for (std::size_t i = 0; i < old.size(); ++i) {
      require(old[i].playerA == zero[i].playerA && old[i].playerB == zero[i].playerB &&
              old[i].resultA == zero[i].resultA, "S0履歴の互換性がありません");
      require(old[i].playerA == other[i].playerA && old[i].playerB == other[i].playerB,
              "条件がマッチング用乱数に影響しました");
      require(other[i].resultA == repeated[i].resultA && other[i].condition == repeated[i].condition,
              "条件・勝敗の再現性がありません");
    }
    const auto conditions = root / "conditions.csv";
    for (const auto* text : {"match_id,z\n0,1\n", "match_id,z\n0,1\n0,-1\n",
        "match_id,z\n0,1\n2,-1\n", "match_id,z\n0,0\n1,1\n",
        "match_id,z\n0,nan\n1,1\n", "wrong\n0,1\n1,-1\n"}) {
      write(conditions, text);
      auto matches = std::vector<Match>{{0, 0, 0, 1, 1}, {1, 1, 0, 1, 0}};
      rejects([&] { io::readConditionsCsv(conditions.string(), matches); });
    }
    // 条件を平均してから二乗すると誤差が隠れる例。正しい条件付きMSEは1/16。
    ev::MseEvaluator plus(0, 1, 1, {{0, .75}, {0, 0}});
    ev::MseEvaluator minus(0, 1, 1, {{0, .25}, {0, 0}});
    plus.addPrediction({0, 1, 0, 1, .5}); minus.addPrediction({0, 1, 0, 1, .5});
    near((plus.finish()[0].mse + minus.finish()[0].mse) / 2, .0625);
    const auto prediction = root / "conditional.csv";
    const std::string header = "run_id,matches_per_player,player_a,player_b,z,predicted_p_a_wins\n";
    const auto checkConditional = [&] {
      ev::MseEvaluator p(0, 1, 1, {{0, .75}, {0, 0}}), n(0, 1, 1, {{0, .25}, {0, 0}});
      io::readConditionalPredictionsCsv(prediction.string(), p, n);
      near(p.finish()[0].mse, 0); near(n.finish()[0].mse, 0);
    };
    write(prediction, header + "0,1,0,1,1,.75\n0,1,0,1,-1,.25\n");
    checkConditional();
    write(prediction, header + "0,1,0,1,1,.75\n"); rejects(checkConditional);
    write(prediction, header + "0,1,0,1,1,.75\n0,1,0,1,1,.75\n"); rejects(checkConditional);

    const auto log = root / "online.csv";
    const std::string logHeader = "run_id,match_id,z,result_a,p_elo,p_advantage\n";
    const std::vector<Match> matches{{0, 0, 0, 1, 1, 1}, {1, 1, 0, 1, 0, -1}};
    write(log, logHeader + "0,0,1,1,.5,.75\n0,1,-1,0,.5,.25\n");
    const auto result = ev::calculateOnlineMetrics(log.string(), 0, matches, 2, 1);
    near(result[1].eloLogLoss, std::log(2));
    near(result[1].advantageLogLoss, -std::log(.75));
    near(result[1].eloBrier, .25); near(result[1].advantageBrier, .0625);
    for (const auto& text : {"0,0,-1,1,.5,.75\n0,1,-1,0,.5,.25\n",
         "0,0,1,0,.5,.75\n0,1,-1,0,.5,.25\n", "0,0,1,1,.5,.75\n",
         "0,0,1,1,.5,.75\n0,0,-1,0,.5,.25\n", "0,0,1,1,nan,.75\n"}) {
      write(log, logHeader + text);
      rejects([&] { ev::calculateOnlineMetrics(log.string(), 0, matches, 2, 1); });
    }
    write(log, logHeader + "0,0,1,1,0,1\n0,1,-1,0,1,0\n");
    const auto edge = ev::calculateOnlineMetrics(log.string(), 0, matches, 2, 1).back();
    near(edge.eloBrier, 1); near(edge.advantageBrier, 0);
    require(std::isfinite(edge.eloLogLoss), "Log Lossの端点が非有限です");

    // 初回予測は更新前: initialAdvantage=400ならz=+1で10/11、z=-1で1/11。
    for (const int z : {1, -1}) {
      const auto csv = root / (z == 1 ? "positive.csv" : "negative.csv");
      fs::remove(csv);
      project10::ratings::AdvantageEloParameters params;
      params.initialAdvantage = 400; params.kAdvantage = 0;
      std::ofstream stream(log);
      io::writeAdvantageEloPredictions(0, {0, 1}, {{0, 0, 0, 1, 1, z}}, 1,
        csv.string(), params, true, &stream);
      stream.close();
      int count = 0;
      io::readOnlinePredictionsCsv(log.string(), [&](const auto& row) {
        near(row.elo, .5); near(row.advantage, z == 1 ? 10.0 / 11 : 1.0 / 11); ++count;
      });
      require(count == 1, "試合前ログの行数が不正です");
      const double pOld = z == 1 ? 10.0 / 11 : 1.0 / 11;
      const double difference = 64 * (1 - pOld);
      const double pPlus = 1 / (1 + std::pow(10.0, -(difference + 400) / 400));
      const double pMinus = 1 / (1 + std::pow(10.0, -(difference - 400) / 400));
      ev::MseEvaluator p(0, 1, 1, {{0, pPlus}, {0, 0}}), n(0, 1, 1, {{0, pMinus}, {0, 0}});
      io::readConditionalPredictionsCsv(csv.string(), p, n);
      require(p.finish()[0].mse < 1e-25 && n.finish()[0].mse < 1e-25,
              "更新に実際の条件zが使われていません");
    }
    // 共通評価サービスへの接続も、既知の2人・1時点の値で独立に照合する。
    const auto work = root / "mse-service";
    fs::create_directories(work / "runs/run_0000");
    const auto saved = work / "runs/run_0000";
    write(saved / "config.json", R"({"run_id":0,"num_players":2,"matches_per_player":1,
      "evaluation_interval_matches_per_player":1,"comparison":{},"files":{
      "players":"players.csv","ground_truth":"ground_truth.csv",
      "predictions":{"Elo":"elo.csv","AdvantageElo":"advantage_elo.csv"},
      "mse":{"Elo":"elo_mse.csv","AdvantageElo":"advantage_elo_mse.csv"}}})");
    writePlayers({0, 1}, (saved / "players.csv").string());
    write(saved / "ground_truth_positive.csv", "player_a,player_b,p_a_wins\n0,1,.75\n");
    write(saved / "ground_truth_negative.csv", "player_a,player_b,p_a_wins\n0,1,.25\n");
    write(saved / "elo.csv", "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n0,1,0,1,.5\n");
    write(saved / "advantage_elo.csv", header + "0,1,0,1,1,.75\n0,1,0,1,-1,.25\n");
    const auto original = fs::current_path();
    fs::current_path(work);
    near(ev::calculateRunMse(0, "Elo")[0].mse, .0625);
    near(ev::calculateRunMse(0, "AdvantageElo")[0].mse, 0);
    fs::current_path(original);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
