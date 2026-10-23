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
        throw std::runtime_error("設定のキーが重複しています: " + key);
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
  throw std::runtime_error(fieldName + "は0以上の整数で指定してください。");
}

// intへ変換する前に、項目ごとの下限とintの上限を確認する。
int readInteger(const json& object, const std::string& fieldName, int minimum) {
  const auto number = readNonNegativeInteger(object, fieldName);
  if (number < static_cast<std::uint64_t>(minimum) ||
      number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error(fieldName + "が許容範囲外です。");
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
    throw std::runtime_error("設定ファイルを開けません: " + path.string());
  }

  std::ostringstream contents;
  contents << file.rdbuf();
  if (file.bad()) {
    throw std::runtime_error("設定ファイルを読み込めません: " + path.string());
  }
  return contents.str();
}

// 未実装の評価工程の設定も、基本的な型と範囲は確認する。
void validateExperimentSettings(const json& config) {
  readInteger(config, "evaluation_interval_matches_per_player", 1);
  for (const auto* field : {"schema_version", "model", "matchmaking"}) {
    const auto& value = config.at(field);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
      throw std::runtime_error(std::string(field) + " must be a non-empty string.");
    }
  }
  if (config.at("model") != "probit_1d") {
    throw std::runtime_error("Only the probit_1d model is implemented.");
  }
  if (config.at("matchmaking") != "uniform_random") {
    throw std::runtime_error("Only uniform_random round matching is implemented.");
  }
}

}  // namespaceおわり

std::string runDirectoryName(int runID) {
  if (runID < 0) {
    throw std::invalid_argument("run IDが不正です。0以上の整数を指定してください。");
  }
  std::string number = std::to_string(runID);
  if (number.size() < 4) {
    number.insert(0, 4 - number.size(), '0');
  }
  return "run_" + number;
}

// JSON読み込みの参考資料と採用範囲はdocs/references.mdを参照。
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
  settings.numPlayers = readInteger(config, "num_players", 2);
  if (settings.numPlayers % 2 != 0) {
    throw std::runtime_error("num_players must be even for round matching.");
  }
  settings.matchesPerPlayer = readInteger(config, "matches_per_player", 1);
  settings.ratingPeriodMatchesPerPlayer = readInteger(
    config, "rating_period_matches_per_player", 1
  );
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

  // 4. 能力・対戦組み合わせ・勝敗それぞれ独立した生成器で使うシード。
  const auto& seeds = config.at("seeds");
  settings.abilitySeed = readNonNegativeInteger(seeds, "ability");
  settings.matchingSeed = readNonNegativeInteger(seeds, "matching");
  settings.resultSeed = readNonNegativeInteger(seeds, "result");

  // 5. 出力先のファイル名を取得し、現在のCSV仕様と一致することを確認する。
  const auto& files = config.at("files");
  settings.playersFile = files.at("players").get<std::string>();
  settings.latentParametersFile = files.at("latent_parameters").get<std::string>();
  settings.groundTruthFile = files.at("ground_truth").get<std::string>();
  settings.matchesFile = files.at("matches").get<std::string>();
  if (settings.playersFile != "players.csv" ||
      settings.latentParametersFile != "latent_parameters.csv" ||
      settings.groundTruthFile != "ground_truth.csv" || settings.matchesFile != "matches.csv") {
    throw std::runtime_error("Unexpected output filenames.");
  }

  return settings;
}

RatingConfig loadTrueSkillConfig(int runID) {
  const auto path = std::filesystem::path("runs") / runDirectoryName(runID) / "config.json";
  const auto config = parseConfig(readConfigText(path));
  RatingConfig result{readInteger(config, "num_players", 2),
                      readInteger(config, "matches_per_player", 1),
                      readInteger(config, "evaluation_interval_matches_per_player", 1),
                      readInteger(config, "rating_period_matches_per_player", 1),
                      config.at("files").at("predictions").at("TrueSkill").get<std::string>()};
  if (readInteger(config, "run_id", 0) != runID || result.numPlayers % 2 != 0 ||
      result.matchesPerPlayer < result.interval) {
    throw std::runtime_error("TrueSkillの設定またはrun IDが不正です。");
  }
  const auto& files = config.at("files");
  if (files.at("players") != "players.csv" || files.at("matches") != "matches.csv" ||
      result.predictionsFile != "trueskill.csv") {
    throw std::runtime_error("TrueSkillのファイル名が規定と一致していません。");
  }
  return result;
}

GlickoConfig loadGlickoConfig(int runID) {
  const auto path = std::filesystem::path("runs") / runDirectoryName(runID) / "config.json";
  try {
    const auto config = parseConfig(readConfigText(path));
    GlickoConfig result{{readInteger(config, "num_players", 2),
                         readInteger(config, "matches_per_player", 1),
                         readInteger(config, "evaluation_interval_matches_per_player", 1),
                         readInteger(config, "rating_period_matches_per_player", 1),
                         config.at("files").at("predictions").at("Glicko").get<std::string>()},
                        0.0, 0.0, 0.0};
    const auto& rating = result.rating;
    if (readInteger(config, "run_id", 0) != runID || rating.numPlayers % 2 != 0 ||
        rating.matchesPerPlayer < rating.interval) {
      throw std::runtime_error("Glickoの設定またはrun IDが不正です。");
    }
    const auto& files = config.at("files");
    if (files.at("players") != "players.csv" || files.at("matches") != "matches.csv" ||
        rating.predictionsFile != "glicko.csv") {
      throw std::runtime_error("Glickoのファイル名が規定と一致していません。");
    }
    // cは未決定のため既定値を置かず、使った値を設定のスナップショットに残す。
    if (!config.contains("glicko")) {
      throw std::runtime_error(
        "設定にglicko（initial_rating・initial_rd・rd_growth）がありません: " + path.string()
      );
    }
    const auto& glicko = config.at("glicko");
    result.initialRating = readFiniteNumber(glicko, "initial_rating");
    result.initialRD = readFiniteNumber(glicko, "initial_rd");
    result.rdGrowth = readFiniteNumber(glicko, "rd_growth");
    if (result.initialRD <= 0.0 || result.rdGrowth < 0.0) {
      throw std::runtime_error("glicko.initial_rdは正、glicko.rd_growthは0以上で指定してください。");
    }
    return result;
  } catch (const json::exception&) {
    throw std::runtime_error("Glicko用の設定JSONの構文・必須項目・型を確認してください: " +
                             path.string());
  }
}

EvaluationConfig loadEvaluationConfig(int runID, const std::string& algorithm) {
  std::string stem;
  if (algorithm == "Elo") stem = "elo";
  else if (algorithm == "Glicko") stem = "glicko";
  else if (algorithm == "Glicko2") stem = "glicko2";
  else if (algorithm == "TrueSkill") stem = "trueskill";
  else throw std::invalid_argument("未対応の手法名です: " + algorithm);

  const auto path = std::filesystem::path("runs") / runDirectoryName(runID) / "config.json";
  try {
    const auto config = parseConfig(readConfigText(path));
    EvaluationConfig settings{};
    settings.runID = readInteger(config, "run_id", 0);
    settings.numPlayers = readInteger(config, "num_players", 2);
    settings.matchesPerPlayer = readInteger(config, "matches_per_player", 1);
    settings.interval = readInteger(config, "evaluation_interval_matches_per_player", 1);
    if (settings.runID != runID || settings.numPlayers % 2 != 0 ||
        settings.matchesPerPlayer < settings.interval) {
      throw std::runtime_error("評価設定が不正か、run IDが一致していません。");
    }
    const auto& files = config.at("files");
    settings.predictionsFile = files.at("predictions").at(algorithm).get<std::string>();
    settings.mseFile = files.at("mse").at(algorithm).get<std::string>();
    // 既存の数値CSV契約に限定し、入力や別手法の結果への書き込みを防ぐ。
    if (files.at("players") != "players.csv" || files.at("ground_truth") != "ground_truth.csv" ||
        settings.predictionsFile != stem + ".csv" || settings.mseFile != stem + "_mse.csv") {
      throw std::runtime_error("評価用のファイル名が規定と一致していません。");
    }
    return settings;
  } catch (const json::parse_error&) {
    throw std::runtime_error("設定JSONの構文が不正です: " + path.string());
  } catch (const json::out_of_range&) {
    throw std::runtime_error("設定JSONに必要な項目がありません: " + path.string());
  } catch (const json::type_error&) {
    throw std::runtime_error("設定JSONの値の型が正しくありません: " + path.string());
  }
}

PlotConfig loadPlotConfig(int runID) {
  const auto path = std::filesystem::path("runs") / runDirectoryName(runID) / "config.json";
  try {
    const auto config = parseConfig(readConfigText(path));
    PlotConfig result{readInteger(config, "run_id", 0),
                      readInteger(config, "matches_per_player", 1),
                      readInteger(config, "evaluation_interval_matches_per_player", 1), {}};
    if (result.runID != runID || result.matchesPerPlayer < result.interval) {
      throw std::runtime_error("描画設定が不正か、run IDが一致していません。");
    }
    const auto& files = config.at("files").at("mse");
    if (!files.is_object()) throw std::runtime_error("files.mseはオブジェクトで指定してください。");
    const std::set<std::string> algorithms{"Elo", "Glicko", "Glicko2", "TrueSkill"};
    std::set<std::string> names;
    for (const auto& item : files.items()) {
      if (algorithms.count(item.key()) == 0) {
        throw std::runtime_error("未対応の手法名です: " + item.key());
      }
      const auto name = item.value().get<std::string>();
      auto foldedName = name;
      for (auto& c : foldedName) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
      // run直下のCSVに限定。外部パス・制御文字・描画コマンドの混入を防ぐ。
      if (name.size() <= 4 || name.substr(name.size() - 4) != ".csv" ||
          name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") !=
            std::string::npos || name.find("..") != std::string::npos ||
          !names.insert(foldedName).second) {
        throw std::runtime_error("MSEファイル名が不正か重複しています: " + name);
      }
      result.mseFiles.emplace(item.key(), name);
    }
    return result;
  } catch (const json::exception&) {
    throw std::runtime_error("描画用の設定JSONの構文・必須項目・型を確認してください: " + path.string());
  }
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
