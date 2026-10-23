#pragma once
#include <filesystem>
namespace fs = std::filesystem;

void generate_glicko2_prediction(int run_id, fs::path file_path);
