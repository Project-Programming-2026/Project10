#pragma once

#include "project10/io/run_lock.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace project10::io {

struct RatingCliOptions {
  bool help = false;
  std::optional<int> runID;
  std::string algorithm;
  std::optional<std::filesystem::path> matchesPath;
  std::optional<std::filesystem::path> outputPath;
  std::optional<std::filesystem::path> conditionsPath;
  std::optional<std::filesystem::path> onlineOutputPath;
  std::optional<std::filesystem::path> ratingOutputPath;
};

// 予測CLI共通の引数解析。requireAlgorithm=trueはproject10_predict用。
RatingCliOptions parseRatingCliOptions(int argc, char** argv, bool requireAlgorithm);
int resolveOrPromptRunID(const RatingCliOptions& options);

// 裸のファイル名はrunDir配下へ、ディレクトリ区切りを含むパスはそのまま解決する。
std::filesystem::path resolveInputCsvPath(const std::filesystem::path& runDir,
  const std::filesystem::path& rawPath, const std::string& label);
std::filesystem::path resolveOutputCsvPath(const std::filesystem::path& runDir,
  const std::filesystem::path& rawPath, const std::string& label);
bool areSameOutputPath(const std::filesystem::path& a, const std::filesystem::path& b);

// ロック領域内の一時ファイルへ書き込み、既存ファイルを上書きしないハードリンクで公開する。
class StagedOutputFile {
public:
  StagedOutputFile(const std::filesystem::path& destination, const std::filesystem::path& runDir,
    const std::filesystem::path& runLockPath, std::string label);
  ~StagedOutputFile();
  StagedOutputFile(const StagedOutputFile&) = delete;
  StagedOutputFile& operator=(const StagedOutputFile&) = delete;

  const std::filesystem::path& tempPath() const { return tempPath_; }
  void publish();

private:
  std::filesystem::path destination_;
  std::string label_;
  std::unique_ptr<RunLock> ownedLock_;
  std::filesystem::path tempPath_;
  bool published_ = false;
};

}  // namespace project10::io
