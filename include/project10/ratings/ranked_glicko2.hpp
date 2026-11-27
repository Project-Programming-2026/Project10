#pragma once
#include <filesystem>
using namespace std;
namespace fs = filesystem;

void generate_ranked_glicko2_prediction(int run_id, fs::path file_path);
void generate_ranked_glicko2_prediction(int run_id, fs::path file_path, fs::path input_file_matches, fs::path output_win_prob_file);
