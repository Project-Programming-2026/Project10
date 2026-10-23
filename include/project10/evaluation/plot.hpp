#pragma once

#include "project10/evaluation/mse.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace project10::evaluation {

// CSVの検証と描画を分離し、将来のrun間集計を描画の前段に追加できるようにする。
struct MseSeries {
  std::string algorithm;
  std::string sourceFile;
  std::vector<MseResult> points;
};

void plotRun(int runID, bool png, const std::string& name,
             const std::string& gnuplot, std::ostream& log);

}  // namespace project10::evaluation
