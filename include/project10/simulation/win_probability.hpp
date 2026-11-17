#pragma once

#include <variant>

namespace project10::simulation {

struct ProbitModel {
  double performanceStd;  // 各人のパフォーマンス標準偏差。
  bool operator==(const ProbitModel& other) const { return performanceStd == other.performanceStd; }
  bool operator!=(const ProbitModel& other) const { return !(*this == other); }
};
struct LogisticModel {
  double scale;  // 誤差差分のロジスティック尺度。標準偏差ではない。
  bool operator==(const LogisticModel& other) const { return scale == other.scale; }
  bool operator!=(const LogisticModel& other) const { return !(*this == other); }
};
using WinProbabilityModel = std::variant<ProbitModel, LogisticModel>;

// thetaA側の勝率。尺度は正の有限値、能力は有限値、有利幅は非負の有限値。
// condition=+1/-1はA/Bが先手、0は両条件の等確率混合。乱数は使わない。
double winProbability(double thetaA, double thetaB, const WinProbabilityModel& model,
                      double sideAdvantage = 0, int condition = 0);

}  // namespace project10::simulation
