#pragma once

// 第7回会議のプロビットモデルで、能力thetaAの側が勝つ確率を返す。
// 能力は有限値、performanceStdは正の有限値。違反時はinvalid_argument。
double calculateWinProbability(
  double thetaA,
  double thetaB,
  double performanceStd
);

// condition=0は両側の等確率混合。advantageは潜在能力と同じ単位。
double calculateConditionalWinProbability(double thetaA, double thetaB, double performanceStd,
                                          double advantage, int condition);
