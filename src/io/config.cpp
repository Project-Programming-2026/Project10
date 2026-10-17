#include "project10/io/config.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace project10::io {
namespace {

using json = nlohmann::json;

// 解析後には重複キーが消えるため、各オブジェクトの解析中に確認する。
json parseConfig(const std::string& text) {
  std::vector<std::set<std::string>> objectKeys;
  return json::parse(text, [&objectKeys](int, json::parse_event_t event, json& value) {
    if (event == json::parse_event_t::object_start) {
      objectKeys.emplace_back();
    } else if (event == json::parse_event_t::object_end) {
      objectKeys.pop_back();
    } else if (event == json::parse_event_t::key) {
      const auto& key = value.get_ref<const std::string&>();
      if (!objectKeys.back().insert(key).second) {
        throw std::runtime_error("Duplicate config key: " + key);
      }
    }
    return true;
  });
}

// JSONの整数は符号付き・符号なしの両方がある。小数や負数を変換で丸めない。
std::uint64_t readNonNegativeInteger(const json& object, const std::string& fieldName) {
  const auto& value = object.at(fieldName);
  if (value.is_number_unsigned()) {
    return value.get<std::uint64_t>();
  }
  if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
    return static_cast<std::uint64_t>(value.get<std::int64_t>());
  }
  throw std::runtime_error(fieldName + " must be a non-negative integer.");
}

// intへ変換する前に、項目ごとの下限とintの上限を確認する。
int readInteger(const json& object, const std::string& fieldName, int minimum) {
  const auto number = readNonNegativeInteger(object, fieldName);
  if (number < static_cast<std::uint64_t>(minimum) ||
      number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error(fieldName + " is out of range.");
  }
  return static_cast<int>(number);
}

double readFiniteNumber(const json& object, const std::string& fieldName) {
  const auto& value = object.at(fieldName);
  if (!value.is_number()) {
    throw std::runtime_error(fieldName + " must be a number.");
  }
  const double number = value.get<double>();
  if (!std::isfinite(number)) {
    throw std::runtime_error(fieldName + " must be finite.");
  }
  return number;
}

// 保存用のスナップショットと解析に、同じ読み込み内容を使う。
std::string readConfigText(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open config file: " + path.string());
  }

  std::ostringstream contents;
  contents << file.rdbuf();
  if (file.bad()) {
    throw std::runtime_error("Cannot read config file: " + path.string());
  }
  return contents.str();
}

// まだ実行しない対戦・評価工程の設定も、基本的な型と範囲は確認する。
void validateExperimentSettings(const json& config) {
  for (const auto* field : {"matches_per_player", "evaluation_interval_matches_per_player",
                            "rating_period_matches_per_player"}) {
    readInteger(config, field, 1);
  }
  for (const auto* field : {"schema_version", "model", "matchmaking"}) {
    const auto& value = config.at(field);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
      throw std::runtime_error(std::string(field) + " must be a non-empty string.");
    }
  }
}

}  // namespaceおわり

std::string runDirectoryName(int runID) {
  if (runID < 0) {
    throw std::invalid_argument("Invalid run ID.");
  }
  std::string number = std::to_string(runID);
  if (number.size() < 4) {
    number.insert(0, 4 - number.size(), '0');
  }
  return "run_" + number;
}

// 参考：Qiita「C++のjsonライブラリ決定版 nlohmann-json」
// https://qiita.com/yohm/items/0f389ba5c5de4e2df9cf
// json::parseによる構文解析、atによる項目取得、getによる型変換を参考にした。
// 本実装では設定値の検証と、読み込んだ設定全体を保存する処理を追加している。
SimulationConfig loadConfig(int runID) {
  // 1. 対象の設定を読み込み、元のJSON全文を保持したまま解析する。
  const auto configPath = std::filesystem::path("configs") /
    runDirectoryName(runID) / "config.json";
  SimulationConfig settings{};
  settings.sourceText = readConfigText(configPath);
  const auto config = parseConfig(settings.sourceText);

  // 2. 実験IDと人数を確認する。必須項目はatで取得し、欠落時は例外にする。
  settings.runID = readInteger(config, "run_id", 0);
  if (settings.runID != runID) {
    throw std::runtime_error("Run ID mismatch.");
  }
  settings.numPlayers = readInteger(config, "num_players", 1);
  validateExperimentSettings(config);
  settings.performanceStd = readFiniteNumber(config, "performance_std");
  if (settings.performanceStd <= 0.0) {
    throw std::runtime_error("performance_std must be positive.");
  }

  // 3. 潜在能力の生成に使う正規分布の平均と標準偏差を取得する。
  const auto& distribution = config.at("ability_distribution");
  if (distribution.at("type") != "normal") {
    throw std::runtime_error("Only the normal ability distribution is implemented.");
  }
  settings.mean = readFiniteNumber(distribution, "mean");
  settings.stddev = readFiniteNumber(distribution, "std");
  if (settings.stddev <= 0.0) {
    throw std::runtime_error("ability_distribution.std must be positive.");
  }

  // 4. 3種類のシードを検証する。現在の生成処理で使うのはabilityのみ。
  const auto& seeds = config.at("seeds");
  settings.abilitySeed = readNonNegativeInteger(seeds, "ability");
  readNonNegativeInteger(seeds, "matching");
  readNonNegativeInteger(seeds, "result");

  // 5. 出力先のファイル名を取得し、現在のCSV仕様と一致することを確認する。
  const auto& files = config.at("files");
  settings.playersFile = files.at("players").get<std::string>();
  settings.latentParametersFile = files.at("latent_parameters").get<std::string>();
  settings.groundTruthFile = files.at("ground_truth").get<std::string>();
  if (settings.playersFile != "players.csv" ||
      settings.latentParametersFile != "latent_parameters.csv" ||
      settings.groundTruthFile != "ground_truth.csv") {
    throw std::runtime_error("Unexpected output filenames.");
  }

  return settings;
}

void writeConfigSnapshot(
  const SimulationConfig& config,
  const std::filesystem::path& outputDir
) {
  std::ofstream file(outputDir / "config.json", std::ios::binary);
  file << config.sourceText;
  file.close();
  if (!file) {
    throw std::runtime_error("Failed to write config snapshot.");
  }
}

}  // namespace project10::ioおわり
