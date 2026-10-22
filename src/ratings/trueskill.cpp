#include "project10/ratings/trueskill.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <stdexcept>

namespace project10::ratings {
namespace {

constexpr double beta = 25.0 / 6.0;
constexpr double tau = 25.0 / 300.0;

void validate(const TrueSkillRating& rating) {
  if (!std::isfinite(rating.mu) || !std::isfinite(rating.sigma) || rating.sigma <= 0.0) {
    throw std::invalid_argument("TrueSkillの平均・標準偏差が不正です。");
  }
}

double normalCdf(double x) {
  return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

}

void updateTrueSkill(TrueSkillRating& a, TrueSkillRating& b, int resultA) {
  validate(a);
  validate(b);
  if (&a == &b || (resultA != 0 && resultA != 1)) {
    throw std::invalid_argument("TrueSkillは1対1・引き分けなしの対戦に限定しています。");
  }
  const double qa = a.sigma * a.sigma + tau * tau;
  const double qb = b.sigma * b.sigma + tau * tau;
  const double c2 = qa + qb + 2.0 * beta * beta;
  const double c = std::sqrt(c2);
  const double sign = resultA == 1 ? 1.0 : -1.0;
  const double t = sign * (a.mu - b.mu) / c;
  if (!std::isfinite(c2) || !std::isfinite(t)) {
    throw std::overflow_error("TrueSkillの更新が数値範囲を超えました。");
  }
  double v;
  double w;
  if (t < -10.0) {
    const double y = -t;
    double fraction = 0.0;
    for (int k = 80; k >= 1; --k) fraction = k / (y + fraction);
    v = y + fraction;
    w = v * fraction;
  } else {
    v = std::exp(-0.5 * t * t) / std::sqrt(2.0 * std::acos(-1.0)) / normalCdf(t);
    w = v * (v + t);
  }
  w = std::clamp(w, 0.0, 1.0);
  const TrueSkillRating nextA{a.mu + sign * qa / c * v,
                              std::sqrt(qa * (1.0 - qa / c2 * w))};
  const TrueSkillRating nextB{b.mu - sign * qb / c * v,
                              std::sqrt(qb * (1.0 - qb / c2 * w))};
  validate(nextA);
  validate(nextB);
  a = nextA;
  b = nextB;
}

double predictTrueSkill(const TrueSkillRating& a, const TrueSkillRating& b) {
  validate(a);
  validate(b);
  const double variance = a.sigma * a.sigma + b.sigma * b.sigma + 2.0 * beta * beta;
  const double t = (a.mu - b.mu) / std::sqrt(variance);
  if (!std::isfinite(variance) || !std::isfinite(t)) {
    throw std::overflow_error("TrueSkillの予測が数値範囲を超えました。");
  }
  return normalCdf(t);
}

void writeTrueSkillPredictions(
  std::ostream& output, int runID, int numPlayers, int interval,
  const std::vector<Match>& matches
) {
  if (runID < 0 || numPlayers < 2 || numPlayers % 2 != 0 || interval <= 0) {
    throw std::invalid_argument("TrueSkillの予測設定が不正です。");
  }
  std::vector<TrueSkillRating> players(numPlayers);
  std::vector<std::uint64_t> counts(numPlayers, 0);
  const auto checkpoint = static_cast<std::uint64_t>(numPlayers / 2) * interval;
  output.imbue(std::locale::classic());
  output << "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n"
         << std::setprecision(std::numeric_limits<double>::max_digits10);
  std::uint64_t processed = 0;
  for (const auto& match : matches) {
    if (match.playerA < 0 || match.playerB < 0 || match.playerA >= numPlayers ||
        match.playerB >= numPlayers) {
      throw std::invalid_argument("未知のプレイヤーIDです。");
    }
    updateTrueSkill(players[match.playerA], players[match.playerB], match.resultA);
    ++counts[match.playerA];
    ++counts[match.playerB];
    if (++processed % checkpoint != 0) continue;
    const auto expected = processed / (numPlayers / 2);
    if (!std::all_of(counts.begin(), counts.end(), [expected](auto n) { return n == expected; })) {
      throw std::invalid_argument("評価時点で各人の対戦数が揃っていません。");
    }
    for (int a = 0; a < numPlayers; ++a) {
      for (int b = a + 1; b < numPlayers; ++b) {
        output << runID << ',' << expected << ',' << a << ',' << b << ','
               << predictTrueSkill(players[a], players[b]) << '\n';
      }
    }
  }
  if (!output) throw std::runtime_error("TrueSkill予測CSVへの書き込みに失敗しました。");
}

}
