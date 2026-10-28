#include "project10/simulation/probit.hpp"

#include <cmath>
#include <stdexcept>

double calculateConditionalWinProbability(double a, double b, double stddev,
                                          double advantage, int condition) {
  if (!std::isfinite(advantage) || advantage < 0 || condition < -1 || condition > 1)
    throw std::invalid_argument("条件付き勝率の設定が不正です。");
  if (advantage == 0) return calculateWinProbability(a, b, stddev);
  if (condition == 0)
    return 0.5 * (calculateConditionalWinProbability(a, b, stddev, advantage, 1) +
                  calculateConditionalWinProbability(a, b, stddev, advantage, -1));
  return calculateWinProbability(a + advantage * condition, b, stddev);
}

double calculateWinProbability(
  double thetaA,
  double thetaB,
  double performanceStd
) {
  if (!std::isfinite(thetaA) ||
      !std::isfinite(thetaB) ||
      !std::isfinite(performanceStd) ||
      performanceStd <= 0.0) {
    throw std::invalid_argument("Invalid probit parameters");
  }

  // 第7回会議の式：Phi((thetaA-thetaB)/(sqrt(2)*sigma_g))。
  // Phi(z)=erfc(-z/sqrt(2))/2なので、erfcの引数は-(thetaA-thetaB)/(2*sigma_g)。
  // 2*sigma_gのオーバーフロー・微小値の丸めを避けるため、割ってから半分にする。
  const double difference = thetaA - thetaB;
  const double halfStandardizedDifference = std::isfinite(difference)
    ? 0.5 * (difference / performanceStd)
    : 0.5 * (thetaA / performanceStd) - 0.5 * (thetaB / performanceStd);
  // 能力差がオーバーフローする場合は、各能力を先に標準化する。
  return 0.5 * std::erfc(-halfStandardizedDifference);
}
