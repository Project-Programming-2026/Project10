// 試作：対戦の組み方による収束の速さの比較（推定方法の比較とは別の実験）。
// 各ラウンドで全員が1試合ずつ行う。組み方は推定レートだけから決め、潜在能力は使わない。
// 勝敗は生成モデルの真の勝率から引く。マッチング用の推定は全履歴推定を毎ラウンド更新する。
// 使い方：matching_probe random|sorted|sampled 事前分布のRD tuning|evaluation [sampledの揺らぎの倍率λ（既定1）]
// sampledは、推定レートにλ × RD × 標準正規乱数を加えた値で並べ、隣どうしを組ませる。
#include "project10/evaluation/mse.hpp"
#include "project10/ratings/glicko2_tunable.hpp"
#include "project10/ratings/whole_history.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/probit.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Seeds {
  unsigned long long ability, matching, result;
};

// 組み方に従って、全員が1試合ずつ行う対戦履歴を作る。
std::vector<Match> simulate(const std::vector<double>& abilities, int rounds, int period,
                            const std::string& matching, double priorRatingDeviation, double noiseScale,
                            const Seeds& seeds) {
  const int n = static_cast<int>(abilities.size());
  std::mt19937_64 matchingGenerator(seeds.matching);
  std::mt19937_64 resultGenerator(seeds.result);
  std::normal_distribution<double> standardNormal(0.0, 1.0);
  project10::ratings::WholeHistoryEstimator estimator(n, priorRatingDeviation);
  std::vector<project10::ratings::WholeHistoryEstimate> estimates(
    static_cast<std::size_t>(n), {1500.0, priorRatingDeviation});
  std::vector<Match> matches;
  for (int round = 0; round < rounds; round++) {
    std::vector<int> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), matchingGenerator);  // randomの組み方・同点の順
    if (matching != "random") {
      std::vector<double> key(static_cast<std::size_t>(n));
      for (int i = 0; i < n; i++) {
        key[i] = estimates[i].rating;
        if (matching == "sampled") {
          key[i] += noiseScale * estimates[i].ratingDeviation * standardNormal(matchingGenerator);
        }
      }
      std::stable_sort(order.begin(), order.end(), [&key](int a, int b) { return key[a] < key[b]; });
    }
    std::vector<Match> roundMatches;
    for (int k = 0; k < n / 2; k++) {
      const int a = order[2 * k];
      const int b = order[2 * k + 1];
      std::bernoulli_distribution win(calculateWinProbability(abilities[a], abilities[b], 1.0));
      roundMatches.push_back({matches.size() + roundMatches.size(), round / period, a, b,
                              win(resultGenerator) ? 1 : 0});
    }
    estimator.addMatches(roundMatches);
    if (matching != "random") estimates = estimator.refit();
    matches.insert(matches.end(), roundMatches.begin(), roundMatches.end());
  }
  return matches;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4 && argc != 5) throw std::invalid_argument("usage: matching_probe random|sorted|sampled RD0 tuning|evaluation [LAMBDA]");
    const double noiseScale = argc == 5 ? std::atof(argv[4]) : 1.0;
    const std::string matching = argv[1];
    if (matching != "random" && matching != "sorted" && matching != "sampled") throw std::invalid_argument("matching");
    const double priorRatingDeviation = std::atof(argv[2]);
    const bool evaluation = std::strcmp(argv[3], "evaluation") == 0;
    std::vector<Seeds> trials;
    for (int t = 1; t <= (evaluation ? 10 : 5); t++) {
      const unsigned long long base = (evaluation ? 2026113000ULL : 2026112500ULL) + 10ULL * t;
      trials.push_back({base + 1, base + 2, base + 3});
    }
    const int n = 1000, rounds = 200, interval = 10, period = 10;
    const project10::ratings::Glicko2Parameters currentGlicko2{350.0, 0.06, 0.5};
    const char* estimators[] = {"whole_history", "glicko2_current"};
    std::vector<std::vector<double>> curves(2, std::vector<double>(rounds / interval, 0.0));
    std::vector<double> totals(2, 0.0);
    for (const auto& seeds : trials) {
      const auto abilities = generateLatentParameters(n, 0.0, 1.0, seeds.ability);
      const auto matches = simulate(abilities, rounds, period, matching, priorRatingDeviation, noiseScale, seeds);
      project10::evaluation::ProbabilityMatrix truth(n, std::vector<double>(n, 0.0));
      for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) truth[a][b] = calculateWinProbability(abilities[a], abilities[b], 1.0);
      for (int e = 0; e < 2; e++) {
        project10::evaluation::MseEvaluator evaluator(1, rounds, interval, truth);
        const auto consume = [&evaluator](int m, int a, int b, double p) { evaluator.addPrediction({1, m, a, b, p}); };
        if (e == 0) project10::ratings::predictWholeHistoryAtCheckpoints(n, interval, matches, priorRatingDeviation, consume);
        else project10::ratings::predictGlicko2AtCheckpoints(n, interval, matches, currentGlicko2, consume);
        const auto results = evaluator.finish();
        double sum = 0.0;
        for (std::size_t k = 0; k < results.size(); k++) {
          sum += results[k].mse;
          curves[e][k] += results[k].mse / trials.size();
        }
        totals[e] += sum / results.size() / trials.size();
        std::printf("trial,%s,%g,%s,%llu,%.17g\n", matching.c_str(), noiseScale, estimators[e], seeds.ability,
                    sum / results.size());
        std::fflush(stdout);
      }
    }
    for (int e = 0; e < 2; e++) {
      for (std::size_t k = 0; k < curves[e].size(); k++)
        std::printf("curve,%s,%g,%s,%zu,%.17g\n", matching.c_str(), noiseScale, estimators[e], (k + 1) * interval,
                    curves[e][k]);
      std::printf("summary,%s,%g,%s,mean_mse=%.7f,@10=%.6f,@50=%.6f,@100=%.6f,@200=%.7f\n", matching.c_str(), noiseScale,
                  estimators[e],
                  totals[e], curves[e][0], curves[e][4], curves[e][9], curves[e][19]);
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
