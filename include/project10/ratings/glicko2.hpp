#pragma once
#include <filesystem>
using namespace std;
namespace fs = filesystem;

void generate_glicko2_prediction(int run_id, fs::path file_path);