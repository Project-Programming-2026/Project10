#include "project10/evaluation/files.hpp"
#include "project10/evaluation/run.hpp"
#include "project10/io/csv.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace project10::evaluation {
namespace {
namespace fs = std::filesystem;

void validateName(const std::string& name) {
  if (name.empty() || name.find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
    throw std::invalid_argument("出力名は英数字・ハイフン・アンダースコアで指定してください。");
}

void validateLabel(const std::string& label) {
  if (label.empty()) throw std::invalid_argument("表示名は空にできません。");
  for (const unsigned char c : label)
    if (c < 32 || c == 127 || c == '`')
      throw std::invalid_argument("表示名に制御文字・バッククォートは使用できません。");
}
}  // namespace

FileOptions parseFileOptions(int argc, char** argv, bool plot) {
  FileOptions options;
  for (int i = 1; i < argc; ++i) options.arguments.emplace_back(argv[i]);
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--help") options.help = true;
    else if (option == "--input" && i + 3 < argc) {
      const std::string label = argv[++i], id = argv[++i];
      int runID = -1;
      const auto parsed = std::from_chars(id.data(), id.data() + id.size(), runID);
      if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() || runID < 0)
        throw std::invalid_argument("run IDが不正です: " + id);
      options.inputs.push_back({label, runID, argv[++i]});
    } else if ((option == "--name" || (plot && option == "--gnuplot")) && i + 1 < argc) {
      auto& value = option == "--name" ? options.name : options.gnuplot;
      if (option == "--name" && !value.empty())
        throw std::invalid_argument("--nameは一度だけ指定してください。");
      value = argv[++i];
      if (value.empty()) throw std::invalid_argument(option + "の値は空にできません。");
    } else if (plot && option == "--png") options.png = true;
    else throw std::invalid_argument("不正なオプション、または引数不足です: " + option);
  }
  if (!options.help && !options.inputs.empty()) validateName(options.name);
  if (!options.help && !plot && options.inputs.empty() && argc > 1)
    throw std::invalid_argument("ファイル指定モードには--inputが必要です。");
  return options;
}

FileComparison::FileComparison(const std::vector<CsvSeriesInput>& inputs, const std::string& name)
  : inputs_(inputs) {
  validateName(name);
  if (inputs.empty()) throw std::invalid_argument("CSVを1系列以上指定してください。");
  if (!fs::is_directory(fs::symlink_status("runs")))
    throw std::invalid_argument("runsは通常のディレクトリにしてください。");
  std::set<int> ids;
  std::set<std::string> labels;
  for (const auto& input : inputs) {
    validateLabel(input.label);
    if (!labels.insert(input.label).second) throw std::invalid_argument("表示名が重複しています。");
    if (input.runID < 0) throw std::invalid_argument("run IDが不正です。");
    if (!fs::is_regular_file(fs::symlink_status(input.path)))
      throw std::invalid_argument("CSV入力は通常のファイルにしてください: " + input.path.string());
    ids.insert(input.runID);
  }
  std::map<int, io::EvaluationContext> saved;
  for (const int id : ids) {
    const auto run = fs::path("runs") / io::runDirectoryName(id);
    if (!fs::is_directory(fs::symlink_status(run)))
      throw std::invalid_argument("runの保存先が通常のディレクトリではありません。");
    locks_.push_back(std::make_unique<io::RunLock>(run.parent_path() / ("." + run.filename().string() + ".lock")));
    saved.emplace(id, io::loadEvaluationContext(id));
  }
  for (const auto& input : inputs) contexts.push_back(saved.at(input.runID));
}

void FileComparison::writeManifest(const fs::path& directory, const std::string& operation,
    const std::vector<std::string>& arguments, const std::vector<std::string>& outputs) const {
  using nlohmann::json;
  json manifest{{"schema_version", 1}, {"operation", operation}, {"arguments", arguments},
    {"working_directory", fs::current_path().string()}, {"outputs", outputs}, {"series", json::array()}};
  for (std::size_t i = 0; i < inputs_.size(); ++i) {
    const auto& input = inputs_[i];
    const auto& context = contexts[i];
    manifest["series"].push_back({{"label", input.label}, {"run_id", input.runID},
      {"input", fs::absolute(input.path).lexically_normal().string()},
      {"mse_definition", context.conditional ? "conditional_equal_sides" : "unconditional"},
      {"config", json::parse(context.sourceText)}});
    if (operation == "evaluate") manifest["series"].back()["output"] = outputs.at(i);
  }
  std::ofstream file(directory / "manifest.json");
  file << manifest.dump(2) << '\n';
  file.close();
  if (!file) throw std::runtime_error("manifest.jsonを保存できません。");
}

ComparisonOutput::ComparisonOutput(const std::string& name, std::vector<std::string> files)
  : files_(std::move(files)) {
  validateName(name);
  const auto parent = fs::path("runs") / "comparisons";
  if (!fs::is_directory(fs::symlink_status("runs")))
    throw std::runtime_error("runsは通常のディレクトリにしてください。");
  if (!fs::create_directory(parent) && !fs::is_directory(fs::symlink_status(parent)))
    throw std::runtime_error("比較の保存先が通常のディレクトリではありません。");
  path = fs::absolute(parent / name);
  if (fs::exists(fs::symlink_status(path)) || !fs::create_directory(path))
    throw std::runtime_error("比較出力先がすでに存在します。--nameで別名を指定してください。");
}

ComparisonOutput::~ComparisonOutput() {
  if (complete_) return;
  std::error_code error;
  for (const auto& file : files_) fs::remove(path / file, error);
  fs::remove(path, error);
}

fs::path evaluateFiles(const std::vector<CsvSeriesInput>& inputs, const std::string& name,
                      const std::vector<std::string>& arguments) {
  FileComparison comparison(inputs, name);
  std::vector<std::vector<MseResult>> results;
  std::vector<std::string> outputs;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const auto conditional = io::hasConditionalPredictions(inputs[i].path.string());
    results.push_back(calculateFileMse(comparison.contexts[i], inputs[i].path, conditional));
    std::ostringstream filename;
    filename << "series_" << std::setfill('0') << std::setw(4) << i + 1 << "_mse.csv";
    outputs.push_back(filename.str());
  }
  auto files = outputs;
  files.push_back("manifest.json");
  ComparisonOutput output(name, files);
  for (std::size_t i = 0; i < inputs.size(); ++i)
    io::writeMseCsv((output.path / outputs[i]).string(), inputs[i].runID, results[i]);
  comparison.writeManifest(output.path, "evaluate", arguments, files);
  output.complete();
  return output.path;
}
}  // namespace project10::evaluation
