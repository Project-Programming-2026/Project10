#pragma once

#include "project10/io/run_lock.hpp"
#include <set>
#include <string>

namespace project10::evaluation {
struct ReusePlan {
  std::set<std::string> predictions;
  std::set<std::string> mse;
  bool onlineMetrics = false;
};

// 読み取りだけで既存成果物を全件検証する。不正なら新規工程を始める前に停止する。
ReusePlan inspectReusableRun(int runID, const io::RunLock& lock, bool allMethods = false,
                             bool comparison = false, int referenceRun = -1);
}  // namespace project10::evaluation
