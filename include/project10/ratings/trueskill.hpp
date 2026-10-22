#pragma once

#include "project10/simulation/matches.hpp"

#include <ostream>
#include <vector>

namespace project10::ratings {

struct TrueSkillRating {
  double mu = 25.0;
  double sigma = 25.0 / 3.0;
};

void updateTrueSkill(TrueSkillRating& a, TrueSkillRating& b, int resultA);
double predictTrueSkill(const TrueSkillRating& a, const TrueSkillRating& b);
void writeTrueSkillPredictions(
  std::ostream& output, int runID, int numPlayers, int interval,
  const std::vector<Match>& matches
);

}
