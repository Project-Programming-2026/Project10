// 全履歴推定の事後最頻値・RD・予測。目的と出典はwhole_history.hppを参照。

#include "project10/ratings/whole_history.hpp"

#include "project10/ratings/glicko.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace project10::ratings {
namespace {

constexpr double unratedRating = 1500.0;  // レート尺度の中心
constexpr double scaleFactor = 173.7178;  // レート尺度とGlicko-2尺度の比（Glicko-2の原典の値）
// 1人ずつのニュートン更新の幅（Glicko-2尺度）がすべてこの値以下になったら収束とする。
// レート尺度では約1.7e-8で、予測勝率への影響は無視できる。
constexpr double tolerance = 1e-10;
// 1,000人・各200試合・事前分布のRD 350でも収束まで900回未満だった。大きく超えたら収束失敗とする。
constexpr int maxSweeps = 100000;

bool isPositiveFinite(double value) {
  return std::isfinite(value) && value > 0.0;
}

// Bradley–Terryモデルの期待スコア。引数はGlicko-2尺度のレート差（自分 − 相手）。
double expectedScore(double scaledDifference) {
  return 1.0 / (1.0 + std::exp(-scaledDifference));
}

// 既知のID・自己対戦でない・勝敗が0か1のすべてを満たすか。
void requireValidMatches(const std::vector<Match>& matches, std::size_t numPlayers) {
  const auto isKnown = [numPlayers](int id) {
    return id >= 0 && static_cast<std::size_t>(id) < numPlayers;
  };
  for (const Match& match : matches) {
    if (!isKnown(match.playerA) || !isKnown(match.playerB) || match.playerA == match.playerB ||
        (match.resultA != 0 && match.resultA != 1)) {
      throw std::invalid_argument("Invalid match for whole-history estimation");
    }
  }
}

}  // namespace

WholeHistoryEstimator::WholeHistoryEstimator(int numPlayers, double priorRatingDeviation) {
  if (numPlayers <= 0 || !isPositiveFinite(priorRatingDeviation)) {
    throw std::invalid_argument("Invalid whole-history parameters");
  }
  const double priorScaledDeviation = priorRatingDeviation / scaleFactor;
  priorPrecision_ = 1.0 / (priorScaledDeviation * priorScaledDeviation);
  if (!isPositiveFinite(priorPrecision_)) {
    throw std::invalid_argument("Invalid whole-history prior");
  }
  const auto size = static_cast<std::size_t>(numPlayers);
  games_.resize(size);
  scaledRatings_.assign(size, 0.0);
  estimates_.assign(size, {unratedRating, priorRatingDeviation});
}

void WholeHistoryEstimator::addMatches(const std::vector<Match>& matches) {
  requireValidMatches(matches, games_.size());  // 途中まで追加した状態を残さないよう、先に検証する。
  for (const Match& match : matches) {
    games_[match.playerA].push_back({match.playerB, static_cast<double>(match.resultA)});
    games_[match.playerB].push_back({match.playerA, 1.0 - match.resultA});
  }
}

const std::vector<WholeHistoryEstimate>& WholeHistoryEstimator::refit() {
  const auto numPlayers = games_.size();
  // 1人ずつ、他の人の推定値を固定したニュートン更新を繰り返す（Gauss–Seidel法）。
  // 対数事後確率は厳密に凹なので、各人の更新は事後最頻値へ向かう。
  for (int sweep = 0;; sweep++) {
    if (sweep == maxSweeps) {
      throw std::runtime_error("Whole-history estimation did not converge");
    }
    double largestStep = 0.0;
    for (std::size_t player = 0; player < numPlayers; player++) {
      const double rating = scaledRatings_[player];
      double gradient = -rating * priorPrecision_;  // 事前分布の中心は0（レート1500）
      double information = priorPrecision_;         // 負のヘッセ行列の対角成分
      for (const Game& game : games_[player]) {
        const double expected = expectedScore(rating - scaledRatings_[game.opponent]);
        gradient += game.score - expected;
        information += expected * (1.0 - expected);
      }
      const double step = gradient / information;
      scaledRatings_[player] = rating + step;
      largestStep = std::max(largestStep, std::abs(step));
    }
    if (!std::isfinite(largestStep)) {
      throw std::runtime_error("Whole-history estimation produced a non-finite value");
    }
    if (largestStep <= tolerance) break;
  }

  // RD：相手を事後最頻値に固定した1人分のヘッセ行列から、事後分布の標準偏差を近似する。
  for (std::size_t player = 0; player < numPlayers; player++) {
    double information = priorPrecision_;
    for (const Game& game : games_[player]) {
      const double expected = expectedScore(scaledRatings_[player] - scaledRatings_[game.opponent]);
      information += expected * (1.0 - expected);
    }
    estimates_[player] = {unratedRating + scaleFactor * scaledRatings_[player],
                          scaleFactor / std::sqrt(information)};
    if (!std::isfinite(estimates_[player].rating) ||
        !isPositiveFinite(estimates_[player].ratingDeviation)) {
      throw std::runtime_error("Whole-history estimation produced a non-finite value");
    }
  }
  return estimates_;
}

double calculateWholeHistoryWinProbability(
  const WholeHistoryEstimate& playerA,
  const WholeHistoryEstimate& playerB
) {
  return calculateGlickoWinProbability({playerA.rating, playerA.ratingDeviation},
                                       {playerB.rating, playerB.ratingDeviation});
}

void predictWholeHistoryAtCheckpoints(
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  double priorRatingDeviation,
  const WholeHistoryPredictionConsumer& consume
) {
  if (interval <= 0) {
    throw std::invalid_argument("Invalid whole-history evaluation interval");
  }
  WholeHistoryEstimator estimator(numPlayers, priorRatingDeviation);
  // 最後の評価時点より後の試合も含めて、添字に使う前に検証する。
  requireValidMatches(matches, static_cast<std::size_t>(numPlayers));
  std::vector<int> counts(static_cast<std::size_t>(numPlayers), 0);  // 各人の累積対戦数
  int checkpoint = interval;  // 次の評価時点（各人の累積対戦数）
  int reached = 0;            // 次の評価時点に達した人数
  std::size_t added = 0;      // estimatorへ追加済みの試合数
  for (std::size_t index = 0; index < matches.size(); index++) {
    for (const int player : {matches[index].playerA, matches[index].playerB}) {
      if (++counts[player] == checkpoint) reached++;
    }
    if (reached < numPlayers) continue;
    if (!std::all_of(counts.begin(), counts.end(), [checkpoint](int count) { return count == checkpoint; })) {
      throw std::invalid_argument("Match counts differ at a whole-history evaluation point");
    }
    estimator.addMatches({matches.begin() + added, matches.begin() + index + 1});
    added = index + 1;
    const auto& estimates = estimator.refit();
    for (int a = 0; a < numPlayers; a++) {
      for (int b = a + 1; b < numPlayers; b++) {
        consume(checkpoint, a, b, calculateWholeHistoryWinProbability(estimates[a], estimates[b]));
      }
    }
    checkpoint += interval;
    reached = 0;
  }
  if (!std::all_of(counts.begin(), counts.end(), [&counts](int count) { return count == counts.front(); })) {
    throw std::invalid_argument("Match counts differ at the end of the whole-history input");
  }
}

}  // namespace project10::ratings
