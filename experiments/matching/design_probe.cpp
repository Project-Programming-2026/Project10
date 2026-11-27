// 試作：対戦の組み方の改良と、上限の目安の測定。
// 各ラウンドで全員が1試合ずつ行う。勝敗は生成モデルの真の勝率から引く。
// 組み方（推定レートだけを使う）：
//   random    無作為に並べて隣どうし
//   sampled   推定レート + λ × RD × 標準正規乱数 で並べて隣どうし
//   offset    推定レートで並べ、奇数ラウンドは1つずらして隣どうし（同じ相手が続くのを避ける）
//   window    推定レートで並べ、w人ずつの組（毎ラウンド開始位置を無作為にずらす）の中で無作為に組ませる
//   mixed     kラウンドに1回はrandom、それ以外はwindow（近い相手の試合に、遠い相手の試合を混ぜる）
// 上限の目安（真の能力を使うため提案手法ではない）：
//   true_sorted  真の能力で並べて隣どうし（毎ラウンド同じ相手になり、対戦の網がつながらない）
//   true_window  真の能力で並べ、windowと同じくw人ずつの組の中で無作為に組ませる
// 評価する推定：whole_history（全履歴推定）、oracle（生成モデルを知る推定器。上限の目安）
// 使い方：design_probe 組み方 tuning|evaluation [--lambda λ（sampled）] [--window w（window・mixed）]
//         [--random-every k（mixed）]
#include "project10/evaluation/mse.hpp"
#include "project10/ratings/whole_history.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/probit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double priorRatingDeviation = 210.0;  // 全履歴推定の事前分布（調整済み）
constexpr double pi = 3.14159265358979323846;

struct Seeds {
  unsigned long long ability, matching, result;
};

double normalCdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }
double normalPdf(double z) { return std::exp(-0.5 * z * z) / std::sqrt(2.0 * pi); }

std::vector<Match> simulate(const std::vector<double>& abilities, int rounds, const std::string& matching,
                            double noiseScale, int windowSize, int randomEvery, const Seeds& seeds) {
  const int n = static_cast<int>(abilities.size());
  std::mt19937_64 matchingGenerator(seeds.matching);
  std::mt19937_64 resultGenerator(seeds.result);
  std::normal_distribution<double> standardNormal(0.0, 1.0);
  project10::ratings::WholeHistoryEstimator estimator(n, priorRatingDeviation);
  std::vector<project10::ratings::WholeHistoryEstimate> estimates(
    static_cast<std::size_t>(n), {1500.0, priorRatingDeviation});
  const bool usesEstimates =
    matching == "sampled" || matching == "offset" || matching == "window" || matching == "mixed";
  std::vector<Match> matches;
  for (int round = 0; round < rounds; round++) {
    std::vector<int> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), matchingGenerator);
    std::vector<double> key(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; i++) {
      if (matching == "true_sorted" || matching == "true_window") key[i] = abilities[i];
      if (usesEstimates) key[i] = estimates[i].rating;
      if (matching == "sampled") key[i] += noiseScale * estimates[i].ratingDeviation * standardNormal(matchingGenerator);
    }
    const bool randomRound = matching == "random" || (matching == "mixed" && round % randomEvery == 0);
    if (!randomRound) {
      std::stable_sort(order.begin(), order.end(), [&key](int a, int b) { return key[a] < key[b]; });
    }
    if (matching == "offset" && round % 2 == 1) {
      std::rotate(order.begin(), order.begin() + 1, order.end());  // 端の2人だけが離れた相手になる
    }
    if (!randomRound && (matching == "window" || matching == "true_window" || matching == "mixed")) {
      // 開始位置をずらしたw人ずつの組の中で並べ替える。先頭の端数（start人）も1つの組とする。
      const int start = std::uniform_int_distribution<int>(0, windowSize - 1)(matchingGenerator);
      for (int begin = 0; begin < n;) {
        const int end = std::min(n, (begin == 0 && start > 0) ? start : begin + windowSize);
        std::shuffle(order.begin() + begin, order.begin() + end, matchingGenerator);
        begin = end;
      }
    }
    std::vector<Match> roundMatches;
    for (int k = 0; k < n / 2; k++) {
      const int a = order[2 * k];
      const int b = order[2 * k + 1];
      std::bernoulli_distribution win(calculateWinProbability(abilities[a], abilities[b], 1.0));
      roundMatches.push_back({matches.size() + roundMatches.size(), round / 10, a, b, win(resultGenerator) ? 1 : 0});
    }
    if (usesEstimates) {
      estimator.addMatches(roundMatches);
      estimates = estimator.refit();
    }
    matches.insert(matches.end(), roundMatches.begin(), roundMatches.end());
  }
  return matches;
}

// 生成モデル（プロビット、能力の事前分布N(0,1)）を知る推定器。上限の目安としてだけ使う。
void predictOracleAtCheckpoints(int n, int interval, const std::vector<Match>& matches,
                                project10::evaluation::MseEvaluator& evaluator) {
  std::vector<std::vector<std::pair<int, double>>> games(static_cast<std::size_t>(n));
  std::vector<double> mean(static_cast<std::size_t>(n), 0.0), variance(static_cast<std::size_t>(n), 0.0);
  const std::size_t perRound = static_cast<std::size_t>(n) / 2;
  std::size_t next = 0;
  for (std::size_t checkpoint = interval; checkpoint * perRound <= matches.size(); checkpoint += interval) {
    for (; next < checkpoint * perRound; next++) {
      const Match& m = matches[next];
      games[m.playerA].push_back({m.playerB, static_cast<double>(m.resultA)});
      games[m.playerB].push_back({m.playerA, 1.0 - m.resultA});
    }
    const auto local = [&](int i, double& gradient, double& information) {
      gradient = -mean[i];
      information = 1.0;
      for (const auto& [j, score] : games[i]) {
        const double z = (mean[i] - mean[j]) / std::sqrt(2.0);
        const double p = std::fmin(std::fmax(normalCdf(z), 1e-300), 1.0 - 1e-16);
        const double density = normalPdf(z);
        gradient += (score - p) * density / (p * (1.0 - p)) / std::sqrt(2.0);
        information += density * density / (p * (1.0 - p)) / 2.0;
      }
    };
    for (int sweep = 0;; sweep++) {
      if (sweep == 100000) throw std::runtime_error("oracle did not converge");
      double largest = 0.0;
      for (int i = 0; i < n; i++) {
        double gradient, information;
        local(i, gradient, information);
        mean[i] += gradient / information;
        largest = std::fmax(largest, std::fabs(gradient / information));
      }
      if (largest <= 1e-10) break;
    }
    for (int i = 0; i < n; i++) {
      double gradient, information;
      local(i, gradient, information);
      variance[i] = 1.0 / information;
    }
    for (int a = 0; a < n; a++)
      for (int b = a + 1; b < n; b++)
        evaluator.addPrediction({1, static_cast<int>(checkpoint), a, b,
                                 normalCdf((mean[a] - mean[b]) / std::sqrt(2.0 + variance[a] + variance[b]))});
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3) throw std::invalid_argument("usage: design_probe MATCHING tuning|evaluation [--lambda X] [--window W]");
    const std::string matching = argv[1];
    const bool windowed = matching == "window" || matching == "true_window" || matching == "mixed";
    if (matching != "random" && matching != "sampled" && matching != "offset" && matching != "true_sorted" &&
        !windowed)
      throw std::invalid_argument("unknown matching");
    const bool evaluation = std::strcmp(argv[2], "evaluation") == 0;
    double noiseScale = 0.0;
    int windowSize = 0;
    int randomEvery = 0;
    for (int i = 3; i + 1 < argc; i += 2) {
      if (std::strcmp(argv[i], "--lambda") == 0 && matching == "sampled") noiseScale = std::atof(argv[i + 1]);
      else if (std::strcmp(argv[i], "--window") == 0 && windowed) windowSize = std::atoi(argv[i + 1]);
      else if (std::strcmp(argv[i], "--random-every") == 0 && matching == "mixed") randomEvery = std::atoi(argv[i + 1]);
      else throw std::invalid_argument("option does not match the matching");
    }
    if ((matching == "sampled" && noiseScale <= 0.0) || (windowed && windowSize < 2) ||
        (matching == "mixed" && randomEvery < 2))
      throw std::invalid_argument("sampled needs --lambda > 0, window needs --window >= 2, mixed needs --random-every >= 2");
    // 出力の識別用。mixedは w * 1000 + k で表す。
    const double setting = matching == "mixed" ? windowSize * 1000.0 + randomEvery : windowed ? windowSize : noiseScale;
    std::vector<Seeds> trials;
    for (int t = 1; t <= (evaluation ? 10 : 5); t++) {
      const unsigned long long base = (evaluation ? 2026113000ULL : 2026112500ULL) + 10ULL * t;
      trials.push_back({base + 1, base + 2, base + 3});
    }
    const int n = 1000, rounds = 200, interval = 10;
    const char* estimators[] = {"whole_history", "oracle"};
    std::vector<std::vector<double>> curves(2, std::vector<double>(rounds / interval, 0.0));
    std::vector<double> totals(2, 0.0);
    for (const auto& seeds : trials) {
      const auto abilities = generateLatentParameters(n, 0.0, 1.0, seeds.ability);
      const auto matches = simulate(abilities, rounds, matching, noiseScale, windowSize, randomEvery, seeds);
      project10::evaluation::ProbabilityMatrix truth(n, std::vector<double>(n, 0.0));
      for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) truth[a][b] = calculateWinProbability(abilities[a], abilities[b], 1.0);
      for (int e = 0; e < 2; e++) {
        project10::evaluation::MseEvaluator evaluator(1, rounds, interval, truth);
        if (e == 0) {
          project10::ratings::predictWholeHistoryAtCheckpoints(n, interval, matches, priorRatingDeviation,
            [&evaluator](int m, int a, int b, double p) { evaluator.addPrediction({1, m, a, b, p}); });
        } else {
          predictOracleAtCheckpoints(n, interval, matches, evaluator);
        }
        const auto results = evaluator.finish();
        double sum = 0.0;
        for (std::size_t k = 0; k < results.size(); k++) {
          sum += results[k].mse;
          curves[e][k] += results[k].mse / trials.size();
        }
        totals[e] += sum / results.size() / trials.size();
        std::printf("trial,%s,%g,%s,%llu,%.17g\n", matching.c_str(), setting, estimators[e], seeds.ability,
                    sum / results.size());
        std::fflush(stdout);
      }
    }
    for (int e = 0; e < 2; e++) {
      for (std::size_t k = 0; k < curves[e].size(); k++)
        std::printf("curve,%s,%g,%s,%zu,%.17g\n", matching.c_str(), setting, estimators[e], (k + 1) * interval,
                    curves[e][k]);
      std::printf("summary,%s,%g,%s,mean_mse=%.7f,@10=%.6f,@50=%.6f,@100=%.6f,@200=%.7f\n", matching.c_str(),
                  setting, estimators[e], totals[e], curves[e][0], curves[e][4], curves[e][9], curves[e][19]);
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
