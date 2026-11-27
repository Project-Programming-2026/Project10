// 試作：評価時点ごとの全履歴の再推定。
// mode=logistic：Glicko-2尺度のロジスティック尤度＋事前分布N(0, φ0²)。予測はg(κ·√(φa²+φb²))で縮める。
// mode=oracle  ：比較の基準。生成モデルのプロビット尤度と能力の事前分布N(0,1)を知っている推定器。
//                予測はガウス事後分布での期待値 Φ(m / √(2σg² + v))。提案手法ではない。
#include "project10/evaluation/mse.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/probit.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr double scaleFactor = 173.7178;
constexpr double pi = 3.14159265358979323846;
struct Seeds { unsigned long long ability, matching, result; };
double normalPdf(double z) { return std::exp(-0.5 * z * z) / std::sqrt(2 * pi); }
double normalCdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }
double g(double phi) { return 1.0 / std::sqrt(1.0 + 3.0 * phi * phi / (pi * pi)); }
}  // namespace

int main(int argc, char** argv) {
  // 使い方：refit_probe logistic|oracle 初期RD κ tuning|evaluation
  // シードはconfigs/glicko2_tuning/のtuning.json・evaluation.jsonと同じ規則（能力・組み合わせ・勝敗）。
  if (argc != 5) throw std::invalid_argument("usage: refit_probe logistic|oracle RD0 KAPPA tuning|evaluation");
  const bool evaluation = std::strcmp(argv[4], "evaluation") == 0;
  std::vector<Seeds> tuning;
  for (int t = 1; t <= (evaluation ? 10 : 5); t++) {
    const unsigned long long base = (evaluation ? 2026113000ULL : 2026112500ULL) + 10ULL * t;
    tuning.push_back({base + 1, base + 2, base + 3});
  }
  const bool oracle = std::strcmp(argv[1], "oracle") == 0;
  const double rd0 = std::atof(argv[2]);
  const double kappa = std::atof(argv[3]);
  const int n = 1000, rounds = 200, interval = 10;
  // 能力の尺度（oracle）またはGlicko-2尺度（logistic）での事前分布の精度
  const double prior = oracle ? 1.0 : 1.0 / std::pow(rd0 / scaleFactor, 2);
  double total = 0;
  std::vector<double> curve(rounds / interval, 0.0);
  for (const auto& seeds : tuning) {
    const auto ab = generateLatentParameters(n, 0, 1, seeds.ability);
    const auto matches = generateMatches(ab, rounds, 10, 1.0, seeds.matching, seeds.result);
    project10::evaluation::ProbabilityMatrix truth(n, std::vector<double>(n, 0));
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) truth[a][b] = calculateWinProbability(ab[a], ab[b], 1.0);
    project10::evaluation::MseEvaluator evaluator(1, rounds, interval, truth);
    std::vector<std::vector<std::pair<int, double>>> games(n);
    std::vector<double> mu(n, 0.0), variance(n, 0.0);
    std::size_t next = 0;
    for (int checkpoint = interval; checkpoint <= rounds; checkpoint += interval) {
      for (const std::size_t end = static_cast<std::size_t>(checkpoint) * n / 2; next < end; next++) {
        const Match& m = matches[next];
        games[m.playerA].push_back({m.playerB, static_cast<double>(m.resultA)});
        games[m.playerB].push_back({m.playerA, 1.0 - m.resultA});
      }
      // 勾配と情報量（logisticはヘッセ行列、oracleはフィッシャー情報量）
      const auto local = [&](int i, double& gradient, double& information) {
        gradient = -mu[i] * prior;
        information = prior;
        for (const auto& [j, score] : games[i]) {
          if (oracle) {
            const double z = (mu[i] - mu[j]) / std::sqrt(2.0);
            const double p = std::fmin(std::fmax(normalCdf(z), 1e-300), 1 - 1e-16);
            const double density = normalPdf(z);
            gradient += (score - p) * density / (p * (1 - p)) / std::sqrt(2.0);
            information += density * density / (p * (1 - p)) / 2.0;
          } else {
            const double e = 1.0 / (1.0 + std::exp(-(mu[i] - mu[j])));
            gradient += score - e;
            information += e * (1 - e);
          }
        }
      };
      int sweep = 0;
      for (double largest = 1; largest > 1e-10; sweep++) {
        if (sweep == 20000) throw std::runtime_error("not converged");
        largest = 0;
        for (int i = 0; i < n; i++) {
          double gradient, information;
          local(i, gradient, information);
          const double step = gradient / information;
          mu[i] += step;
          largest = std::fmax(largest, std::fabs(step));
        }
      }
      for (int i = 0; i < n; i++) {
        double gradient, information;
        local(i, gradient, information);
        variance[i] = 1.0 / information;
      }
      for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
          const double difference = mu[a] - mu[b], spread = variance[a] + variance[b];
          const double p = oracle ? normalCdf(difference / std::sqrt(2.0 + spread))
                                  : 1.0 / (1.0 + std::exp(-g(kappa * std::sqrt(spread)) * difference));
          evaluator.addPrediction({1, checkpoint, a, b, p});
        }
    }
    const auto results = evaluator.finish();
    double sum = 0;
    for (std::size_t k = 0; k < results.size(); k++) { sum += results[k].mse; curve[k] += results[k].mse / tuning.size(); }
    total += sum / results.size();
    std::printf("trial,%s,%g,%llu,%.17g\n", argv[1], rd0, seeds.ability, sum / results.size());
  }
  for (std::size_t k = 0; k < curve.size(); k++)
    std::printf("curve,%s,%g,%zu,%.17g\n", argv[1], rd0, (k + 1) * interval, curve[k]);
  std::printf("%s rd0=%g kappa=%g mean_mse=%.7f @10=%.6f @50=%.6f @100=%.6f @200=%.7f\n", argv[1], rd0, kappa,
              total / tuning.size(), curve[0], curve[4], curve[9], curve[19]);
}
