// Glicko-2の収束を速めるパラメータ調整実験。1回の実行で、設定に列挙した試行（シードの組）と
// パラメータの組をすべて評価する。試行ごとにデータを1回だけ生成し、全パラメータへ同じ履歴を渡す。
// 生成・勝率・MSEは既存の関数を使い、正解勝率は共通のMSE評価器だけに渡す。
// 使い方：project10_glicko2_tuning 設定JSON 出力ディレクトリ（出力先は未作成であること）

#include "project10/evaluation/mse.hpp"
#include "project10/ratings/glicko2_tunable.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/matches.hpp"
#include "project10/simulation/probit.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using project10::ratings::Glicko2Parameters;

struct TrialSeeds {
  std::uint64_t ability;
  std::uint64_t matching;
  std::uint64_t result;
};

struct TuningConfig {
  std::string sourceText;  // 出力へそのまま保存する
  int numPlayers;
  int matchesPerPlayer;
  int ratingPeriodMatchesPerPlayer;
  int evaluationInterval;
  double abilityMean;
  double abilityStd;
  double performanceStd;
  std::vector<TrialSeeds> trials;
  std::vector<Glicko2Parameters> parameterSets;
};

// 解析後は重複キーが消えるため、解析中に確認する。
json parseWithoutDuplicateKeys(const std::string& text) {
  std::vector<std::set<std::string>> objectKeys;
  return json::parse(text, [&objectKeys](int, json::parse_event_t event, json& value) {
    if (event == json::parse_event_t::object_start) {
      objectKeys.emplace_back();
    } else if (event == json::parse_event_t::object_end) {
      objectKeys.pop_back();
    } else if (event == json::parse_event_t::key &&
               !objectKeys.back().insert(value.get<std::string>()).second) {
      throw std::runtime_error("設定のキーが重複しています: " + value.get<std::string>());
    }
    return true;
  });
}

// 書き間違えた項目を黙って無視しないよう、未知のキーを拒否する。
void requireExactKeys(const json& object, const std::set<std::string>& keys, const std::string& name) {
  if (!object.is_object()) throw std::runtime_error(name + "はオブジェクトで指定してください。");
  for (const auto& item : object.items()) {
    if (!keys.count(item.key())) throw std::runtime_error(name + "に未知の項目があります: " + item.key());
  }
  for (const auto& key : keys) {
    if (!object.contains(key)) throw std::runtime_error(name + "に項目がありません: " + key);
  }
}

// 小数や負数を変換で丸めない。
std::uint64_t readNonNegativeInteger(const json& object, const std::string& key) {
  const auto& value = object.at(key);
  if (value.is_number_unsigned()) return value.get<std::uint64_t>();
  if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
    return static_cast<std::uint64_t>(value.get<std::int64_t>());
  }
  throw std::runtime_error(key + "は0以上の整数で指定してください。");
}

int readPositiveInteger(const json& object, const std::string& key) {
  const auto value = readNonNegativeInteger(object, key);
  if (value == 0 || value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error(key + "は正のint範囲の整数で指定してください。");
  }
  return static_cast<int>(value);
}

double readFiniteNumber(const json& value, const std::string& key) {
  if (!value.is_number() || !std::isfinite(value.get<double>())) {
    throw std::runtime_error(key + "は有限の数値で指定してください。");
  }
  return value.get<double>();
}

double readPositiveNumber(const json& value, const std::string& key) {
  const double number = readFiniteNumber(value, key);
  if (number <= 0.0) throw std::runtime_error(key + "は正の数値で指定してください。");
  return number;
}

// 各値は数値1つか数値の配列。配列は全組み合わせへ展開する。
std::vector<double> readCandidates(const json& value, const std::string& key) {
  if (!value.is_array()) return {readPositiveNumber(value, key)};
  if (value.empty()) throw std::runtime_error(key + "の候補が空です。");
  std::vector<double> candidates;
  for (const auto& item : value) candidates.push_back(readPositiveNumber(item, key));
  return candidates;
}

TuningConfig loadTuningConfig(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream contents;
  contents << file.rdbuf();
  if (!file) throw std::runtime_error("設定ファイルを読み込めません: " + path.string());

  TuningConfig config{};
  config.sourceText = contents.str();
  const json root = parseWithoutDuplicateKeys(config.sourceText);
  requireExactKeys(root, {"schema_version", "num_players", "matches_per_player",
    "rating_period_matches_per_player", "evaluation_interval_matches_per_player", "model",
    "ability_distribution", "performance_std", "matchmaking", "trials",
    "glicko2_parameter_sets"}, "設定");
  if (root.at("schema_version") != "1.0" || root.at("model") != "probit_1d" ||
      root.at("matchmaking") != "uniform_random") {
    throw std::runtime_error("schema_version 1.0、probit_1d、uniform_randomだけに対応しています。");
  }
  config.numPlayers = readPositiveInteger(root, "num_players");
  config.matchesPerPlayer = readPositiveInteger(root, "matches_per_player");
  config.ratingPeriodMatchesPerPlayer = readPositiveInteger(root, "rating_period_matches_per_player");
  config.evaluationInterval = readPositiveInteger(root, "evaluation_interval_matches_per_player");
  const auto& distribution = root.at("ability_distribution");
  requireExactKeys(distribution, {"type", "mean", "std"}, "ability_distribution");
  if (distribution.at("type") != "normal") throw std::runtime_error("能力分布はnormalだけに対応しています。");
  config.abilityMean = readFiniteNumber(distribution.at("mean"), "mean");
  config.abilityStd = readPositiveNumber(distribution.at("std"), "std");
  config.performanceStd = readPositiveNumber(root.at("performance_std"), "performance_std");

  // 複数試行の平均とばらつきを報告するため、異なるシードの組を2つ以上求める。
  for (const auto* key : {"trials", "glicko2_parameter_sets"}) {
    if (!root.at(key).is_array()) throw std::runtime_error(std::string(key) + "は配列で指定してください。");
  }
  std::set<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> seenTrials;
  for (const auto& trial : root.at("trials")) {
    requireExactKeys(trial, {"ability", "matching", "result"}, "trials");
    const TrialSeeds seeds{readNonNegativeInteger(trial, "ability"),
                           readNonNegativeInteger(trial, "matching"),
                           readNonNegativeInteger(trial, "result")};
    if (!seenTrials.insert({seeds.ability, seeds.matching, seeds.result}).second) {
      throw std::runtime_error("同じシードの組の試行が重複しています。");
    }
    config.trials.push_back(seeds);
  }
  if (config.trials.size() < 2) throw std::runtime_error("trialsには2つ以上の試行を指定してください。");

  std::set<std::tuple<double, double, double>> seenSets;
  for (const auto& group : root.at("glicko2_parameter_sets")) {
    requireExactKeys(group, {"initial_rd", "initial_volatility", "tau"}, "glicko2_parameter_sets");
    for (const double rd : readCandidates(group.at("initial_rd"), "initial_rd")) {
      for (const double volatility : readCandidates(group.at("initial_volatility"), "initial_volatility")) {
        for (const double tau : readCandidates(group.at("tau"), "tau")) {
          if (!seenSets.insert({rd, volatility, tau}).second) {
            throw std::runtime_error("同じパラメータの組が重複しています。");
          }
          config.parameterSets.push_back({rd, volatility, tau});
        }
      }
    }
  }
  if (config.parameterSets.empty()) throw std::runtime_error("パラメータの組がありません。");
  return config;
}

// 1試行・1パラメータの組について、各評価時点のMSEを返す。
std::vector<double> evaluateParameterSet(const TuningConfig& config, int trialID,
    const std::vector<Match>& matches, const project10::evaluation::ProbabilityMatrix& truth,
    const Glicko2Parameters& parameters) {
  project10::evaluation::MseEvaluator evaluator(trialID, config.matchesPerPlayer,
                                                config.evaluationInterval, truth);
  project10::ratings::predictGlicko2AtCheckpoints(config.numPlayers, config.evaluationInterval,
    matches, parameters, [&](int matchesPerPlayer, int playerA, int playerB, double probability) {
      evaluator.addPrediction({trialID, matchesPerPlayer, playerA, playerB, probability});
    });
  std::vector<double> mse;
  for (const auto& result : evaluator.finish()) mse.push_back(result.mse);
  return mse;
}

double mean(const std::vector<double>& values) {
  double sum = 0.0;
  for (const double value : values) sum += value;
  return sum / static_cast<double>(values.size());
}

double sampleStandardDeviation(const std::vector<double>& values) {
  const double center = mean(values);
  double sum = 0.0;
  for (const double value : values) sum += (value - center) * (value - center);
  return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

// 各行の先頭のパラメータ列。設定に書いた10進数の値を、余分な桁なしで書く。
void writeParameters(std::ostream& output, std::size_t setID, const Glicko2Parameters& parameters) {
  const auto precision = output.precision(15);
  output << setID + 1 << ',' << parameters.initialRatingDeviation << ','
         << parameters.initialVolatility << ',' << parameters.tau << ',';
  output.precision(precision);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::invalid_argument("使い方: project10_glicko2_tuning 設定JSON 出力ディレクトリ");
    }
    const fs::path outputDir = argv[2];
    if (fs::exists(fs::symlink_status(outputDir))) {
      throw std::runtime_error("出力先がすでに存在します: " + outputDir.string());
    }
    const auto config = loadTuningConfig(argv[1]);

    // mse[パラメータの組][試行][評価時点]。計算をすべて終えてから出力する。
    std::vector<std::vector<std::vector<double>>> mse(config.parameterSets.size());
    for (std::size_t trial = 0; trial < config.trials.size(); trial++) {
      const auto& seeds = config.trials[trial];
      const auto abilities = generateLatentParameters(
        config.numPlayers, config.abilityMean, config.abilityStd, seeds.ability);
      const auto matches = generateMatches(abilities, config.matchesPerPlayer,
        config.ratingPeriodMatchesPerPlayer, config.performanceStd, seeds.matching, seeds.result);
      // 正解勝率は評価器だけへ渡し、Glicko-2には対戦履歴だけを渡す。
      project10::evaluation::ProbabilityMatrix truth(
        abilities.size(), std::vector<double>(abilities.size(), 0.0));
      for (std::size_t a = 0; a < abilities.size(); a++) {
        for (std::size_t b = a + 1; b < abilities.size(); b++) {
          truth[a][b] = calculateWinProbability(abilities[a], abilities[b], config.performanceStd);
        }
      }
      for (std::size_t set = 0; set < config.parameterSets.size(); set++) {
        mse[set].push_back(evaluateParameterSet(config, static_cast<int>(trial + 1), matches,
                                                truth, config.parameterSets[set]));
      }
      std::cerr << "trial " << trial + 1 << '/' << config.trials.size() << " done\n";
    }

    if (outputDir.has_parent_path()) fs::create_directories(outputDir.parent_path());
    if (!fs::create_directory(outputDir)) {
      throw std::runtime_error("出力先を作成できません: " + outputDir.string());
    }
    std::ofstream snapshot(outputDir / "config.json", std::ios::binary);
    snapshot << config.sourceText;
    std::ofstream details(outputDir / "mse.csv");      // 試行・評価時点ごとのMSE
    std::ofstream trials(outputDir / "trials.csv");    // 試行ごとの全評価時点の平均
    std::ofstream curves(outputDir / "curves.csv");    // 評価時点ごとの試行間の平均
    std::ofstream summary(outputDir / "summary.csv");  // パラメータの組ごとの集計
    const std::string parameterHeader = "set_id,initial_rd,initial_volatility,tau,";
    details << parameterHeader << "trial,matches_per_player,mse\n";
    trials << parameterHeader << "trial,mean_mse\n";
    curves << parameterHeader << "matches_per_player,mean_mse,mse_sd\n";
    summary << parameterHeader << "trials,mean_mse,mean_mse_sd,first_mse,last_mse\n";
    for (auto* output : {&details, &trials, &curves, &summary}) {
      output->imbue(std::locale::classic());
      *output << std::setprecision(std::numeric_limits<double>::max_digits10);
    }

    std::size_t best = 0;
    double bestMeanMse = std::numeric_limits<double>::infinity();
    for (std::size_t set = 0; set < mse.size(); set++) {
      const auto& parameters = config.parameterSets[set];
      // 選択基準：試行ごとに全評価時点のMSEを平均し、さらに試行間で平均する。
      std::vector<double> trialMeans;
      for (std::size_t trial = 0; trial < mse[set].size(); trial++) {
        const auto& curve = mse[set][trial];
        for (std::size_t point = 0; point < curve.size(); point++) {
          writeParameters(details, set, parameters);
          details << trial + 1 << ',' << (point + 1) * config.evaluationInterval << ','
                  << curve[point] << '\n';
        }
        trialMeans.push_back(mean(curve));
        writeParameters(trials, set, parameters);
        trials << trial + 1 << ',' << trialMeans.back() << '\n';
      }
      std::vector<double> pointMeans;
      for (std::size_t point = 0; point < mse[set].front().size(); point++) {
        std::vector<double> values;
        for (const auto& curve : mse[set]) values.push_back(curve[point]);
        pointMeans.push_back(mean(values));
        writeParameters(curves, set, parameters);
        curves << (point + 1) * config.evaluationInterval << ',' << pointMeans.back() << ','
               << sampleStandardDeviation(values) << '\n';
      }
      const double meanMse = mean(trialMeans);
      writeParameters(summary, set, parameters);
      summary << trialMeans.size() << ',' << meanMse << ',' << sampleStandardDeviation(trialMeans)
              << ',' << pointMeans.front() << ',' << pointMeans.back() << '\n';
      if (meanMse < bestMeanMse) {
        bestMeanMse = meanMse;
        best = set;
      }
    }
    for (auto* output : {&snapshot, &details, &trials, &curves, &summary}) {
      output->close();
      if (!*output) throw std::runtime_error("結果を保存できません: " + outputDir.string());
    }
    const auto& chosen = config.parameterSets[best];
    std::cout << "best set_id=" << best + 1 << " initial_rd=" << chosen.initialRatingDeviation
              << " initial_volatility=" << chosen.initialVolatility << " tau=" << chosen.tau
              << " mean_mse=" << bestMeanMse << '\n';
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
