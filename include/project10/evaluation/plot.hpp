#pragma once

#include "project10/evaluation/mse.hpp"
#include "project10/evaluation/ranking.hpp"
#include "project10/evaluation/files.hpp"
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

struct RankingSeries {
  std::string algorithm;
  std::string sourceFile;
  std::vector<RankingResult> points;
};

// borrowedLock指定時は対象runとの一致を検査し、取得・解放は呼び出し側に任せる。
void plotRun(int runID, bool png, const std::string& name,
             const std::string& gnuplot, std::ostream& log,
             const io::RunLock* borrowedLock = nullptr, bool reuseExisting = false,
             bool ranking = false);

std::filesystem::path plotFiles(const std::vector<CsvSeriesInput>& inputs,
  const std::string& name, bool png, const std::string& gnuplot,
  const std::vector<std::string>& arguments = {}, bool ranking = false);

}  // namespace project10::evaluation
