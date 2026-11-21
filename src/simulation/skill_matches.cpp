#include "project10/io/config.hpp"
#include "project10/simulation/skill_matches.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/probit.hpp"
#include "project10/io/csv.hpp"

#include "project10/ratings/glicko2.hpp"

#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <random>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <set>
#include <numeric>
#include <limits>
#include <cmath>
using namespace std;
namespace fs = filesystem;

namespace{

struct Player{
    int id;
    double rating;
    double parameter;
};

vector<pair<int,int>> makeSkillPairs(
    const vector<Player>& players,
    const set<pair<int,int>>& playedPairs
){
    const int n = static_cast<int>(players.size());

    if (n % 2 != 0) {
        throw std::invalid_argument(
            "プレイヤー数は偶数である必要があります"
        );
    }

    std::vector<pair<int,int>> pairs;
    std::vector<bool> used(n, false);

    for (int i = 0; i < n; ++i) {
        if (used[i]) continue;
        int best = -1;
        bool bestRepeated = true;
        double bestDiff = std::numeric_limits<double>::infinity();
        for (int j = i + 1; j < n; ++j) {
            if (used[j]) continue;
            pair<int,int> key = std::minmax(
                players[i].id, players[j].id
            );
            bool repeated = playedPairs.count(key) > 0;
            double diff = std::abs(
                players[i].rating - players[j].rating
            );
            if (best == -1 ||
                (bestRepeated && !repeated) ||
                (bestRepeated == repeated && diff < bestDiff)) {
                best = j;
                bestRepeated = repeated;
                bestDiff = diff;
            }
        }
        if (best == -1) throw std::logic_error("対戦相手が見つかりません");
        used[i] = true;
        used[best] = true;
        pairs.emplace_back(i, best);
    }
    return pairs;
}

void simurate(
    RatingSystem& system,
    int runID
){
    auto config = project10::io::loadConfig(runID,"runs");
    std::mt19937_64 resultRng(config.resultSeed);
    std::mt19937_64 conditionRng(config.conditionSeed);
    std::bernoulli_distribution firstSide(0.5);

    string str;
    cout << "Output file name?: ";
    cin >> str;
    fs::path outputFileName(str);

    int ratingPeriodMatches = 1;
    cout << "Do you use rating period?(y/other): ";
    cin >> str;
    if(str == "y"){
        ratingPeriodMatches = config.ratingPeriodMatchesPerPlayer;
        cout << "rating period: true" << endl;
    }else cout << "rating period: false" << endl;
    if (ratingPeriodMatches <= 0 || config.matchesPerPlayer % ratingPeriodMatches != 0) {
        throw std::invalid_argument("レーティング期間の設定が不正です");
    }

    auto runDir = fs::path("runs") / project10::io::runDirectoryName(runID);

    map<int,double> parameters;
    ifstream in_parameters(runDir / fs::path("latent_parameters.csv"));
    if(!in_parameters) throw runtime_error("latent_parameters.csvを開けません");
    string line;
    getline(in_parameters,line);
    while(getline(in_parameters,line)){
        stringstream ss(line);
        string value;
        vector<string> row;
        while(getline(ss,value,',')) row.push_back(value);
        if(row.size() != 2) throw runtime_error("latent_parameters.csvのデータが不正です");
        parameters[stoi(row.at(0))] = stod(row.at(1));
    }
    in_parameters.close();

    vector<Player> players;
    ifstream in_players(runDir / fs::path("players.csv"));
    if(!in_players) throw runtime_error("players.csvを開けません");
    getline(in_players,line);
    while(getline(in_players,line)){
        int id = stoi(line);
        Player p;
        p.id = id;
        p.rating = system.getRating(id);
        p.parameter = parameters.at(id);
        players.push_back(p);
    }
    in_players.close();


    vector<Match> totalMatches;
    std::mt19937_64 matchingRng(config.matchingSeed);
    for(int i = 0; i < config.matchesPerPlayer / ratingPeriodMatches; i++){
        shuffle(players.begin(), players.end(), matchingRng);
        stable_sort(players.begin(),players.end(),[](Player& p1, Player& p2){return p1.rating < p2.rating;});
        vector<Match> matches;
        /*
        for(int j = 1; j <= ratingPeriodMatches; j++){
            int startIdx = 0;
            int idx = 0;
            for(int k = 0; k < players.size(); k+=2){
                const int condition = config.conditional ? (firstSide(conditionRng) ? 1 : -1) : 1;
                const double probability = calculateConditionalWinProbability(
                    players.at(idx).parameter, players.at(idx + j).parameter, config.performanceStd, config.sideAdvantage, condition
                );
                std::bernoulli_distribution result(probability);
                matches.push_back({
                    static_cast<std::uint64_t>(totalMatches.size() + matches.size()), i,
                    players.at(idx).id, players.at(idx + j).id, result(resultRng) ? 1 : 0, condition
                });
                idx += 2 * j;
                if(idx > players.size() - j){
                    startIdx++;
                    idx = startIdx;
                }
            }
        }
        */
        set<pair<int,int>> playedPairs;
        for (int j = 0; j < ratingPeriodMatches; ++j){
            vector<pair<int,int>> pairs = makeSkillPairs(players,playedPairs);
            for(pair<int,int> pair: pairs){
                playedPairs.insert(minmax(players[pair.first].id, players[pair.second].id));
                const int condition = config.conditional ? (firstSide(conditionRng) ? 1 : -1) : 1;
                const double probability = calculateConditionalWinProbability(
                    players.at(pair.first).parameter, players.at(pair.second).parameter, config.performanceStd, config.sideAdvantage, condition
                );
                std::bernoulli_distribution result(probability);
                matches.push_back({
                    static_cast<std::uint64_t>(totalMatches.size() + matches.size()), i,
                    players.at(pair.first).id, players.at(pair.second).id, result(resultRng) ? 1 : 0, condition
                });
            }
        }
        system.update(matches);
        for(Player& p:players) p.rating = system.getRating(p.id);
        totalMatches.insert(totalMatches.end(),matches.begin(),matches.end());
    }
    writeMatches(totalMatches, (runDir / outputFileName).string());
    if(config.conditional) project10::io::writeConditions(totalMatches, (runDir / "conditions_S.csv").string());
}

}

void generateSkillMatches(int runID){
    string algorithm;
    cout << "Enter algorithm (Elo/Glicko/Glicko2/TrueSkill/AdvantageElo/rankedGlicko2): "; //アルゴリズムの追加につきここに追記
    cin >> algorithm;
    
    auto runDir = fs::path("runs") / project10::io::runDirectoryName(runID);
    //スキルマッチに対応した手法は以下を更新
    if(algorithm == "Elo") throw runtime_error("スキルマッチ未対応の手法です");
    else if(algorithm == "Glicko") throw runtime_error("スキルマッチ未対応の手法です");
    else if(algorithm == "Glicko2") {Glicko2System system(runDir); simurate(system,runID);}
    else if(algorithm == "TrueSkill") throw runtime_error("スキルマッチ未対応の手法です");
    else if(algorithm == "AdvantageElo") throw runtime_error("スキルマッチ未対応の手法です");
    else if(algorithm == "rankedGlicko2") throw runtime_error("スキルマッチ未対応の手法です");
    else throw invalid_argument("存在しない手法です。");

    cout << "skill-matching succeeded" << endl;
}