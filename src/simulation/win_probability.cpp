#include "project10/simulation/win_probability.hpp"
#include "project10/simulation/probit.hpp"

#include <cmath>
#include <stdexcept>

namespace project10::simulation {
namespace {
// (a-b+shift)/scale。最大の異符号項を先に相殺し、巨大な同符号和は縮小して割る。
// long doubleの幅には依存しない。通常域・微小値では分子を縮小しない。
double standardizedDifference(double a, double b, double shift, double scale) {
  const double terms[]{a, -b, shift};
  int positive = -1, negative = -1;
  for (int i = 0; i < 3; ++i) {
    if (terms[i] > 0 && (positive < 0 || terms[i] > terms[positive])) positive = i;
    if (terms[i] < 0 && (negative < 0 || terms[i] < terms[negative])) negative = i;
  }
  if (positive >= 0 && negative >= 0) {
    const double cancelled = terms[positive] + terms[negative];
    const double remaining = terms[3 - positive - negative];
    const double sum = cancelled + remaining;
    if (std::isfinite(sum)) return sum / scale;
    return 2 * ((0.5 * cancelled) / scale + (0.5 * remaining) / scale);
  }
  const double sum = (terms[0] + terms[1]) + terms[2];
  if (std::isfinite(sum)) return sum / scale;
  return 4 * ((0.25 * terms[0]) / scale + (0.25 * terms[1]) / scale +
              (0.25 * terms[2]) / scale);
}

double logistic(double a, double b, double scale, double advantage, int condition) {
  if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(scale) || scale <= 0 ||
      !std::isfinite(advantage) || advantage < 0 || condition < -1 || condition > 1)
    throw std::invalid_argument("Invalid logistic parameters");
  if (advantage != 0 && condition == 0)
    return 0.5 * (logistic(a, b, scale, advantage, 1) + logistic(a, b, scale, advantage, -1));
  const double x = standardizedDifference(a, b, advantage * condition, scale);
  // NIST LOGCDF (1997), Eq.8-261/262。expの引数は常に非正とし、クリップしない。
  const double e = std::exp(-std::abs(x));
  return x >= 0 ? 1 / (1 + e) : e / (1 + e);
}
}  // namespace

double winProbability(double a, double b, const WinProbabilityModel& model,
                      double advantage, int condition) {
  if (const auto* probit = std::get_if<ProbitModel>(&model))
    return calculateConditionalWinProbability(a, b, probit->performanceStd, advantage, condition);
  return logistic(a, b, std::get<LogisticModel>(model).scale, advantage, condition);
}
}  // namespace project10::simulation
