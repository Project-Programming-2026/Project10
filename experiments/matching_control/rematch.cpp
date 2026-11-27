// 対照実験用：正式な生成器で作ったrunのmatches.csvを、推定レートの近い人どうしを組む方式で作り直す。
// 各推定は、そのラウンドまでに観測した試合結果だけから求める。真の能力は勝敗を引くときだけ使う。
// 使い方：rematch runディレクトリ 組み方に使う手法 λ
//   手法：Elo | Glicko | Glicko2 | TrueSkill | WholeHistory
//   毎ラウンド、全員を無作為に並べ（同点の順）、「レート + λ × 不確実性 × 標準正規乱数」の小さい順に
//   並べ替えて隣どうしを組ませる。乱数は毎ラウンド引き直す。
//     Glicko・Glicko2・WholeHistoryはRD（レート尺度）、TrueSkillはσ（μの尺度）を不確実性とする。
//     Eloは不確実性を持たないため、λをそのままEloポイントの標準偏差として使う。
// 推定器の設定は、評価に使う正式なCLIと同じ値にする：
//   Elo 初期1500・K 32・尺度400（EloParametersの既定値、毎試合更新）
//   Glicko 設定のglicko（実験では1500・350・rd_growth 0）、期間ごとに更新
//   Glicko2 RD 350・σ 0.06・τ 0.5、期間ごとに更新。正式なglicko2.cppはrunのファイル単位でしか動かないため、
//     同じ式を持つ調整実験用の実装（コミット787c686のglicko2_tunable）を使う
//   TrueSkill μ 25・σ 25/3（TrueSkillRatingの既定値、毎試合更新）
//   WholeHistory 事前分布のRD 210、毎ラウンド推定し直す
// 期間更新の手法は、期間の途中では期間開始時のレートで組む。
// 能力はlatent_parameters.csvを読み、設定のシードで再生成した値と一致するか確認する。
// 乱数：組み方（シャッフルと揺らぎ）は設定のmatchingシード、勝敗は設定のresultシード。勝敗は正式な生成器と
// 同じく、winProbabilityで求めた勝率のベルヌーイ分布で引く。
// 最後に、組み方に使った最終状態からの予測をrematch_check.csvに書き、正式なCLIの予測と照合できるようにする。
#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/ratings/elo.hpp"
#include "project10/ratings/glicko.hpp"
#include "project10/ratings/glicko2_tunable.hpp"
#include "project10/ratings/trueskill.hpp"
#include "project10/ratings/whole_history.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/win_probability.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace pr = project10::ratings;

namespace {

// 手法ごとの推定状態。rating()とspread()で並べる値を作り、observeRound()で1ラウンド分を観測する。
class Estimator {
public:
  Estimator(const std::string& method, int n, double initialRating, double initialRD, double rdGrowth)
    : method_(method), elo_(n), glicko_(initializeGlickoStates(n, initialRating, initialRD)),
      glicko2_(pr::initializeGlicko2States(n, glicko2Parameters_)), trueskill_(static_cast<std::size_t>(n)),
      wh_(n, wholeHistoryPriorRD), whEstimates_(static_cast<std::size_t>(n), {1500.0, wholeHistoryPriorRD}),
      initialRD_(initialRD), rdGrowth_(rdGrowth) {
    if (method != "Elo" && method != "Glicko" && method != "Glicko2" && method != "TrueSkill" &&
        method != "WholeHistory") {
      throw std::invalid_argument("unknown method");
    }
  }

  double rating(int i) const {
    if (method_ == "Elo") return elo_.rating(i);
    if (method_ == "Glicko") return glicko_[i].rating;
    if (method_ == "Glicko2") return glicko2_[i].rating;
    if (method_ == "TrueSkill") return trueskill_[i].mu;
    return whEstimates_[i].rating;
  }

  double spread(int i) const {
    if (method_ == "Elo") return 1.0;
    if (method_ == "Glicko") return glicko_[i].rd;
    if (method_ == "Glicko2") return glicko2_[i].ratingDeviation;
    if (method_ == "TrueSkill") return trueskill_[i].sigma;
    return whEstimates_[i].ratingDeviation;
  }

  // 1ラウンド分の試合を観測する。periodEndsなら期間更新の手法を更新する。
  void observeRound(const std::vector<Match>& round, bool periodEnds) {
    if (method_ == "Elo") {
      for (const auto& m : round) elo_.updateMatch(m);
    } else if (method_ == "TrueSkill") {
      for (const auto& m : round) pr::updateTrueSkill(trueskill_[m.playerA], trueskill_[m.playerB], m.resultA);
    } else if (method_ == "WholeHistory") {
      wh_.addMatches(round);
      whEstimates_ = wh_.refit();
    } else {
      period_.insert(period_.end(), round.begin(), round.end());
      if (periodEnds) {
        if (method_ == "Glicko") glicko_ = updateGlickoPeriod(glicko_, period_, initialRD_, rdGrowth_);
        else glicko2_ = pr::updateGlicko2Period(glicko2_, period_, glicko2Parameters_.tau);
        period_.clear();
      }
    }
  }

  double predict(int a, int b) const {
    if (method_ == "Elo") return elo_.predictWinProbability(a, b);
    if (method_ == "Glicko") return calculateGlickoWinProbability(glicko_[a], glicko_[b]);
    if (method_ == "Glicko2") return pr::calculateGlicko2WinProbability(glicko2_[a], glicko2_[b]);
    if (method_ == "TrueSkill") return pr::predictTrueSkill(trueskill_[a], trueskill_[b]);
    return pr::calculateWholeHistoryWinProbability(whEstimates_[a], whEstimates_[b]);
  }

private:
  static constexpr double wholeHistoryPriorRD = 210.0;
  static constexpr pr::Glicko2Parameters glicko2Parameters_{350.0, 0.06, 0.5};
  std::string method_;
  pr::EloRating elo_;
  std::vector<GlickoState> glicko_;
  std::vector<pr::Glicko2State> glicko2_;
  std::vector<pr::TrueSkillRating> trueskill_;
  pr::WholeHistoryEstimator wh_;
  std::vector<pr::WholeHistoryEstimate> whEstimates_;
  std::vector<Match> period_;
  double initialRD_;
  double rdGrowth_;
};

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4) throw std::invalid_argument("usage: rematch RUN_DIR METHOD LAMBDA");
    const fs::path dir = argv[1];
    const std::string method = argv[2];
    const double noiseScale = std::stod(argv[3]);
    if (!(noiseScale >= 0.0)) throw std::invalid_argument("bad lambda");

    const auto config = project10::io::loadSimulationConfigFile(dir / "config.json");
    if (config.conditional) throw std::invalid_argument("conditional runs are not supported");
    const int n = config.numPlayers;
    const auto abilities = project10::io::readLatentParametersCsv((dir / "latent_parameters.csv").string(), n);
    if (abilities != generateLatentParameters(n, config.mean, config.stddev, config.abilitySeed)) {
      throw std::runtime_error("latent parameters do not match the configured seed");
    }
    // Glickoの初期値は正式なCLIと同じく設定のglickoを読む。
    std::ifstream configFile(dir / "config.json");
    const auto json = nlohmann::json::parse(configFile);
    const double initialRating = json.at("glicko").at("initial_rating").get<double>();
    const double initialRD = json.at("glicko").at("initial_rd").get<double>();
    const double rdGrowth = json.at("glicko").at("rd_growth").get<double>();

    Estimator estimator(method, n, initialRating, initialRD, rdGrowth);
    std::mt19937_64 matchingRng(config.matchingSeed);
    std::mt19937_64 resultRng(config.resultSeed);
    std::normal_distribution<double> standardNormal(0.0, 1.0);
    std::vector<Match> matches;
    std::vector<int> order(static_cast<std::size_t>(n));
    std::vector<double> key(static_cast<std::size_t>(n));
    const int period = config.ratingPeriodMatchesPerPlayer;
    for (int round = 0; round < config.matchesPerPlayer; round++) {
      std::iota(order.begin(), order.end(), 0);
      std::shuffle(order.begin(), order.end(), matchingRng);
      for (int i = 0; i < n; i++) {
        key[i] = estimator.rating(i) + noiseScale * estimator.spread(i) * standardNormal(matchingRng);
      }
      std::stable_sort(order.begin(), order.end(), [&key](int a, int b) { return key[a] < key[b]; });
      std::vector<Match> roundMatches;
      for (int k = 0; k < n / 2; k++) {
        const int a = order[2 * k];
        const int b = order[2 * k + 1];
        std::bernoulli_distribution result(
          project10::simulation::winProbability(abilities[a], abilities[b], config.winModel));
        roundMatches.push_back({static_cast<std::uint64_t>(matches.size() + roundMatches.size()), round / period,
                                a, b, result(resultRng) ? 1 : 0});
      }
      estimator.observeRound(roundMatches, (round + 1) % period == 0);
      matches.insert(matches.end(), roundMatches.begin(), roundMatches.end());
    }

    const auto temporary = dir / "matches.rematch.tmp";
    writeMatches(matches, temporary.string());
    project10::io::readMatchesCsv(temporary.string(), n, config.matchesPerPlayer, period);
    fs::rename(temporary, dir / config.matchesFile);

    std::ofstream check(dir / "rematch_check.csv");
    check << "player_a,player_b,predicted_p_a_wins\n" << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (int a = 0; a < n; a += 37) {
      for (int b = a + 1; b < n; b += 53) check << a << ',' << b << ',' << estimator.predict(a, b) << '\n';
    }
    std::cout << "rematched by " << method << " lambda=" << noiseScale << " in " << dir.string() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
