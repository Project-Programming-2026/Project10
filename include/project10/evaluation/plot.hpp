#pragma once

#include "project10/evaluation/mse.hpp"
#include "project10/io/run_lock.hpp"

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

// borrowedLock指定時は対象runとの一致を検査し、取得・解放は呼び出し側に任せる。
void plotRun(int runID, bool png, const std::string& name,
             const std::string& gnuplot, std::ostream& log,
             const io::RunLock* borrowedLock = nullptr);

}  // namespace project10::evaluation
