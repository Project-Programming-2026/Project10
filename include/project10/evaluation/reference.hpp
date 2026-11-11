#pragma once
#include "project10/io/config.hpp"
#include <filesystem>
#include <string>

namespace project10::evaluation {
struct ReferenceData { std::string config, ratings; };
// 参照用runを個別に指定した旧方式の成果物について、描画前に保存済み参照データを検証する。
void validateSavedReference(const io::SimulationConfig& target, int referenceRun);
std::filesystem::path referenceDirectory(int targetRun, int referenceRun);
// samples/glicko_baselineの固定分布。元ファイルを変更せず、使用した設定・レートをrun内に保存する。
ReferenceData captureFixedReference(const io::SimulationConfig& target);
void validateFixedReference(int runID);
std::filesystem::path fixedReferenceDirectory(int runID);
}  // namespace project10::evaluation
