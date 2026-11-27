#pragma once

#include "project10/simulation/matches.hpp"
#include <vector>

class RatingSystem{
public:
    virtual ~RatingSystem() = default;

    // 現在の推定レートを取得
    virtual double getRating(int playerId) const = 0;

    // 1期間分の試合結果を受け取り、状態を更新
    virtual void update(
        const std::vector<Match>& matches
    ) = 0;
};

void generateSkillMatches(int runID);