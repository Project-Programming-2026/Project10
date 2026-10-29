// ranked_glicko（Glicko初代版）の分布倍率と期間更新。
// 目的と数式の出典はranked_glicko.hppを参照。

#include "project10/ratings/ranked_glicko.hpp"

#include <cmath>
#include <cstddef>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr double lowestBandEdge = 950.0;  // 帯1の下端。これ未満は帯0
constexpr double bandWidth = 100.0;

// レートの一覧から、帯ごとの人数割合（合計1）を求める。
std::vector<double> ratiosOf(const std::vector<double>& ratings) {
  if (ratings.empty()) {
    throw std::invalid_argument("At least one player is required");
  }
  std::vector<double> ratios(rankedGlickoBandCount, 0.0);
  for (const double rating : ratings) {
    ratios[rankedGlickoBand(rating)] += 1.0;  // まず人数を数える。
  }
  for (double& ratio : ratios) {
    ratio /= static_cast<double>(ratings.size());
  }
  return ratios;
}

// 基準ファイルの1行を有限なレートとして読む。Windowsの改行（CRLF）も受け付ける。
double parseRating(std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  std::istringstream input(line);
  input.imbue(std::locale::classic());
  double rating;
  if (!(input >> rating) || !(input >> std::ws).eof() || !std::isfinite(rating)) {
    throw std::invalid_argument("Reference rating must be one finite number per line: " + line);
  }
  return rating;
}

// 基準の割合が13帯分あり、各帯が0〜1、合計が1であること。
void validateReferenceRatios(const std::vector<double>& ratios) {
  if (ratios.size() != static_cast<std::size_t>(rankedGlickoBandCount)) {
    throw std::invalid_argument("referenceRatios must have one value per band");
  }
  double total = 0.0;
  for (const double ratio : ratios) {
    if (!std::isfinite(ratio) || ratio < 0.0 || ratio > 1.0) {
      throw std::invalid_argument("Each reference ratio must be between 0 and 1");
    }
    total += ratio;
  }
  if (std::abs(total - 1.0) > 1e-9) {
    throw std::invalid_argument("Reference ratios must sum to 1");
  }
}

}  // namespace

int rankedGlickoBand(double rating) {
  if (!std::isfinite(rating)) {
    throw std::invalid_argument("Rating must be finite");
  }
  const double position = std::floor((rating - lowestBandEdge) / bandWidth) + 1.0;
  if (position < 0.0) return 0;
  if (position > rankedGlickoBandCount - 1) return rankedGlickoBandCount - 1;
  return static_cast<int>(position);
}

std::vector<double> calculateBandRatios(const std::vector<GlickoState>& states) {
  std::vector<double> ratings;
  for (const GlickoState& state : states) {
    ratings.push_back(state.rating);
  }
  return ratiosOf(ratings);
}

std::vector<double> readReferenceRatios(std::istream& input) {
  std::string line;
  if (!std::getline(input, line) || (line != "rating" && line != "rating\r")) {
    throw std::invalid_argument("Reference rating file must start with the header 'rating'");
  }
  std::vector<double> ratings;
  while (std::getline(input, line)) {
    ratings.push_back(parseRating(line));
  }
  if (!input.eof()) {
    throw std::runtime_error("Failed to read reference ratings");
  }
  return ratiosOf(ratings);
}

double calculateDistributionMultiplier(
  double currentRatio,
  double referenceRatio,
  double alpha,
  double sensitivity
) {
  if (!std::isfinite(currentRatio) || currentRatio < 0.0 || currentRatio > 1.0 ||
      !std::isfinite(referenceRatio) || referenceRatio < 0.0 || referenceRatio > 1.0 ||
      !std::isfinite(alpha) || alpha < 0.0 || !std::isfinite(sensitivity) || sensitivity <= 0.0) {
    throw std::invalid_argument("Invalid distribution multiplier parameters");
  }
  // e/(e + sensitivity²)は、ずれ0で0、ずれが大きいほど1に近づく。Dは1以上1 + α以下。
  const double squaredError = (currentRatio - referenceRatio) * (currentRatio - referenceRatio);
  return 1.0 + alpha * squaredError / (squaredError + sensitivity * sensitivity);
}

std::vector<GlickoState> updateRankedGlickoPeriod(
  const std::vector<GlickoState>& states,
  const std::vector<Match>& periodMatches,
  const std::vector<double>& referenceRatios,
  const RankedGlickoParameters& parameters
) {
  validateReferenceRatios(referenceRatios);
  // 通常のGlickoの更新（対戦・状態の検証も含む）。Step 1はRDだけを変えるので、
  // 更新前のレートはstatesのままである。
  const auto glickoStates = updateGlickoPeriod(
    states, periodMatches, parameters.maxRD, parameters.rdGrowth
  );
  const auto currentRatios = calculateBandRatios(states);  // 更新前の全員の割合P
  auto nextStates = glickoStates;  // RDは通常のGlickoの値を引き継ぐ。
  for (std::size_t i = 0; i < states.size(); i++) {
    const int band = rankedGlickoBand(states[i].rating);  // 更新前の所属帯
    const double multiplier = calculateDistributionMultiplier(
      currentRatios[band], referenceRatios[band], parameters.alpha, parameters.sensitivity
    );
    const double glickoChange = glickoStates[i].rating - states[i].rating;
    nextStates[i].rating = states[i].rating + multiplier * glickoChange;
    if (!std::isfinite(nextStates[i].rating)) {
      throw std::runtime_error("Ranked Glicko update produced a non-finite rating");
    }
  }
  return nextStates;
}

void writeRankedGlickoPredictions(
  std::ostream& output,
  int runID,
  int numPlayers,
  int interval,
  const std::vector<Match>& matches,
  const std::vector<double>& referenceRatios,
  const RankedGlickoParameters& parameters
) {
  validateReferenceRatios(referenceRatios);
  writeGlickoPredictionsWith(
    output, runID, interval, matches,
    initializeGlickoStates(numPlayers, parameters.initialRating, parameters.maxRD),
    [&referenceRatios, &parameters](
      const std::vector<GlickoState>& states, const std::vector<Match>& period
    ) {
      return updateRankedGlickoPeriod(states, period, referenceRatios, parameters);
    }
  );
}
