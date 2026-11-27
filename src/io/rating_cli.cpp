#include "project10/io/rating_cli.hpp"

#include <charconv>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace project10::io {
namespace {
namespace fs = std::filesystem;

int parseNonNegativeInt(const std::string& text, const char* message) {
  int value = -1;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 0)
    throw std::invalid_argument(message);
  return value;
}

void assignOptionPath(std::optional<fs::path>& target, int& i, int argc, char** argv, const std::string& opt) {
  if (target) throw std::invalid_argument(opt + " は一度だけ指定してください。");
  if (i + 1 >= argc) throw std::invalid_argument(opt + " の値がありません。");
  const std::string value = argv[++i];
  if (value.empty()) throw std::invalid_argument(opt + " の値は空にできません。");
  target = fs::path(value);
}

fs::path resolveRunScopedPath(const fs::path& runDir, const fs::path& rawPath, const std::string& label) {
  if (rawPath.empty()) throw std::invalid_argument(label + "のパスが空です。");
  const fs::path name = rawPath.filename();
  if (name.empty() || name == "." || name == "..")
    throw std::invalid_argument(label + "にはファイル名を含むパスを指定してください: " + rawPath.string());
  return (!rawPath.has_parent_path() && !rawPath.has_root_path() && !rawPath.has_root_name())
    ? (runDir / rawPath) : rawPath;
}

fs::path normalizedParent(const fs::path& p) { return p.parent_path().empty() ? fs::path(".") : p.parent_path(); }

void rejectRelativeSymlinkAncestors(const fs::path& path, const std::string& label) {
  if (path.is_absolute()) return;
  fs::path cur;
  for (const auto& part : path.parent_path()) {
    cur /= part;
    if (!cur.empty() && part != "." && fs::is_symlink(fs::symlink_status(cur)))
      throw std::runtime_error(label + "の経路にシンボリックリンクは指定できません: " + cur.string());
  }
}

}  // namespace

RatingCliOptions parseRatingCliOptions(int argc, char** argv, bool requireAlgorithm) {
  RatingCliOptions options;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") options.help = true;
    else if (arg == "--matches") assignOptionPath(options.matchesPath, i, argc, argv, arg);
    else if (arg == "--output") assignOptionPath(options.outputPath, i, argc, argv, arg);
    else if (requireAlgorithm && arg == "--conditions") assignOptionPath(options.conditionsPath, i, argc, argv, arg);
    else if (requireAlgorithm && arg == "--online-output") assignOptionPath(options.onlineOutputPath, i, argc, argv, arg);
    else if (requireAlgorithm && arg == "--rating-output") assignOptionPath(options.ratingOutputPath, i, argc, argv, arg);
    else if (arg.rfind("--", 0) == 0) throw std::invalid_argument("不正なオプションです: " + arg);
    else positional.push_back(arg);
  }
  if (options.help) return options;
  if (requireAlgorithm) {
    if (positional.size() != 2)
      throw std::invalid_argument("project10_predict runID Elo|Glicko2|AdvantageElo|rankedGlicko2 [オプション]");
    options.runID = parseNonNegativeInt(positional[0], "run IDが不正です。");
    options.algorithm = positional[1];
    if (options.algorithm != "Elo" && options.algorithm != "Glicko2" &&
        options.algorithm != "rankedGlicko2" && options.algorithm != "AdvantageElo")
      throw std::invalid_argument("未対応の手法名です。");
    if (options.ratingOutputPath && options.algorithm != "Glicko2")
      throw std::invalid_argument("--rating-output は Glicko2 でのみ指定できます。");
    if ((options.conditionsPath || options.onlineOutputPath) && options.algorithm != "AdvantageElo")
      throw std::invalid_argument("--conditions と --online-output は AdvantageElo でのみ指定できます。");
  } else {
    if (positional.size() > 1) throw std::invalid_argument("位置引数が多すぎます。");
    if (positional.size() == 1)
      options.runID = parseNonNegativeInt(positional[0], "run IDが不正です。0以上の整数を入力してください。");
  }
  return options;
}

int resolveOrPromptRunID(const RatingCliOptions& options) {
  if (options.runID.has_value()) return *options.runID;
  std::string input;
  std::cout << "Enter run ID: ";
  std::getline(std::cin, input);
  std::istringstream parser(input);
  int runID = -1;
  if (!(parser >> runID) || runID < 0 || !(parser >> std::ws).eof())
    throw std::invalid_argument("run IDが不正です。0以上の整数を入力してください。");
  return runID;
}

fs::path resolveInputCsvPath(const fs::path& runDir, const fs::path& rawPath, const std::string& label) {
  const fs::path resolved = resolveRunScopedPath(runDir, rawPath, label);
  rejectRelativeSymlinkAncestors(resolved, label);
  const fs::path parent = normalizedParent(resolved);
  const auto parentStatus = fs::symlink_status(parent);
  if (fs::exists(parentStatus) && (fs::is_symlink(parentStatus) || !fs::is_directory(parentStatus)))
    throw std::runtime_error(label + "の親ディレクトリが通常のディレクトリではありません: " + parent.string());
  const auto status = fs::symlink_status(resolved);
  if (!fs::exists(status)) throw std::runtime_error(label + "が見つかりません: " + resolved.string());
  if (!fs::is_regular_file(status))
    throw std::runtime_error(label + "にはシンボリックリンクではない通常のファイルを指定してください: " + resolved.string());
  return resolved;
}

fs::path resolveOutputCsvPath(const fs::path& runDir, const fs::path& rawPath, const std::string& label) {
  const fs::path resolved = resolveRunScopedPath(runDir, rawPath, label);
  rejectRelativeSymlinkAncestors(resolved, label);
  const fs::path parent = normalizedParent(resolved);
  const auto parentStatus = fs::symlink_status(parent);
  if (fs::is_symlink(parentStatus) || !fs::is_directory(parentStatus))
    throw std::runtime_error(label + "の保存先ディレクトリが存在しないか、通常のディレクトリではありません: " + parent.string());
  if (fs::exists(fs::symlink_status(resolved)))
    throw std::runtime_error(label + "がすでに存在します: " + resolved.string());
  return resolved;
}

bool areSameOutputPath(const fs::path& a, const fs::path& b) {
  if (a.filename() != b.filename()) return false;
  std::error_code ec;
  const bool sameParent = fs::equivalent(normalizedParent(a), normalizedParent(b), ec);
  return ec ? (a.lexically_normal() == b.lexically_normal()) : sameParent;
}

StagedOutputFile::StagedOutputFile(const fs::path& destination, const fs::path& runDir,
  const fs::path& runLockPath, std::string label)
  : destination_(destination), label_(std::move(label)) {
  const fs::path destParent = normalizedParent(destination_);
  std::error_code ec;
  if (fs::equivalent(destParent, runDir, ec) && !ec) {
    tempPath_ = runLockPath / destination_.filename();
  } else {
    const fs::path lockDir = destParent / ("." + destination_.filename().string() + ".project10.lock");
    ownedLock_ = std::make_unique<RunLock>(lockDir);
    tempPath_ = lockDir / destination_.filename();
  }
  if (fs::exists(fs::symlink_status(tempPath_)))
    throw std::runtime_error(label_ + "の一時ファイルがすでに存在します: " + tempPath_.string());
}

StagedOutputFile::~StagedOutputFile() {
  if (!published_ && !tempPath_.empty()) {
    std::error_code ignored;
    fs::remove(tempPath_, ignored);
  }
  ownedLock_.reset();
}

void StagedOutputFile::publish() {
  std::error_code error;
  fs::create_hard_link(tempPath_, destination_, error);
  if (error == std::errc::file_exists)
    throw std::runtime_error(label_ + "がすでに存在します: " + destination_.string());
  if (error)
    throw std::runtime_error(label_ + "を上書きせずに配置できません（エラーコード: " +
      std::to_string(error.value()) + "）: " + destination_.string());
  published_ = true;
  std::error_code cleanupError;
  fs::remove(tempPath_, cleanupError);
  if (cleanupError) std::cerr << "一時ファイルを削除できません: " << tempPath_.string() << '\n';
}

}  // namespace project10::io
