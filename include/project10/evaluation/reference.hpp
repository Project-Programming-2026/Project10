#pragma once
#include "project10/io/config.hpp"
#include <filesystem>
#include <string>

namespace project10::evaluation {
struct ReferenceData { std::string config, ratings; };
// PR #28以前の保存済み成果物を描画するときの検査。
void validateSavedReference(const io::SimulationConfig& target, int referenceRun);
std::filesystem::path referenceDirectory(int targetRun, int referenceRun);
// samples/glicko_baselineの固定分布。元ファイルを変更せず、使用した設定・レートをrun内に保存する。
ReferenceData captureFixedReference(const io::SimulationConfig& target);
void validateFixedReference(int runID);
std::filesystem::path fixedReferenceDirectory(int runID);
}  // namespace project10::evaluation
