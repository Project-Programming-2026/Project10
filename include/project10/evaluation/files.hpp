#pragma once

#include "project10/io/config.hpp"
#include "project10/io/run_lock.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace project10::evaluation {

struct CsvSeriesInput {
  std::string label;
  int runID;
  std::filesystem::path path;
};

struct FileOptions {
  std::vector<CsvSeriesInput> inputs;
  std::string name;
  bool png = false;
  std::string gnuplot = "gnuplot";
  bool help = false;
  std::vector<std::string> arguments;
};

// 両CLI共通の引数検証。plot=falseでは描画専用オプションを拒否する。
FileOptions parseFileOptions(int argc, char** argv, bool plot);

// ID順のrunロックを保持し、検証に使った設定の同じ内容をmanifestへ保存する。
class FileComparison {
public:
  FileComparison(const std::vector<CsvSeriesInput>& inputs, const std::string& name);
  std::vector<io::EvaluationContext> contexts;
  void writeManifest(const std::filesystem::path& directory, const std::string& operation,
                     const std::vector<std::string>& arguments,
                     const std::vector<std::string>& outputs) const;
private:
  std::vector<CsvSeriesInput> inputs_;
  std::vector<std::unique_ptr<io::RunLock>> locks_;
};

// 自身で予約した新規ディレクトリと既知のファイルだけを失敗時に片付ける。
class ComparisonOutput {
public:
  ComparisonOutput(const std::string& name, std::vector<std::string> files);
  ~ComparisonOutput();
  ComparisonOutput(const ComparisonOutput&) = delete;
  ComparisonOutput& operator=(const ComparisonOutput&) = delete;
  std::filesystem::path path;
  void complete() { complete_ = true; }
private:
  std::vector<std::string> files_;
  bool complete_ = false;
};

std::filesystem::path evaluateFiles(const std::vector<CsvSeriesInput>& inputs,
  const std::string& name, const std::vector<std::string>& arguments = {});

}  // namespace project10::evaluation
