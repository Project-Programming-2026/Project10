#pragma once
#include "project10/simulation/skill_matches.hpp"
#include <filesystem>
using namespace std;
namespace fs = filesystem;

void generate_glicko2_prediction(int run_id, fs::path file_path);

class Glicko2System: public RatingSystem{
public:
    Glicko2System(fs::path runDir);
    ~Glicko2System() override;
    double getRating(int playerId) const override;
    void update(const vector<Match>& matches) override;
};
