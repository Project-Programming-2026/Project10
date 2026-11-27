#include "project10/simulation/skill_matches.hpp"

#include <iostream>
using namespace std;

int main(){
    int runID;
    cout << "Enter runID: ";
    cin >> runID;

    generateSkillMatches(runID);
}