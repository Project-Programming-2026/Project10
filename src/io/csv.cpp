#include "project10/io/csv.hpp"
#include "project10/io/run_lock.hpp"
#include "project10/simulation/probit.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <locale>
#include <map>
#include <sstream>

// CSV入出力の参考資料はdocs/references.mdを参照。
void writePlayers(const std::vector<int>& players, const std::string& outputPath) {
  std::ofstream file(outputPath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + outputPath);
  }
  file << "player_id\n";
  for (const int player : players) {
    file << player << '\n';
  }
  file.close();
  if (!file) {
    throw std::runtime_error("Failed to write: " + outputPath);
  }
}

void writeMatches(const std::vector<Match>& matches, const std::string& outputPath) {
  std::ofstream file(outputPath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + outputPath);
  }
  file << "match_id,period_id,player_a,player_b,result_a\n";
  for (const auto& match : matches) {
    file << match.matchID << ',' << match.periodID << ',' << match.playerA << ','
      << match.playerB << ',' << match.resultA << '\n';
  }
  file.close();
  if (!file) {
    throw std::runtime_error("Failed to write: " + outputPath);
  }
}

void writeLatentParameters(
  const std::vector<double>& parameters,
  const std::string& outputPath
) {
  std::ofstream file(outputPath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + outputPath);
  }
  file << "player_id,theta_0\n";
  file << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (std::size_t i = 0; i < parameters.size(); i++) {
    file << i << ',' << parameters[i] << '\n';
  }
  file.close();
  if (!file) {
    throw std::runtime_error("Failed to write: " + outputPath);
  }
}

void writeGroundTruth(
  const std::vector<double>& parameters,
  double performanceStd,
  const std::string& outputPath, double sideAdvantage, int condition
) {
  std::ofstream file(outputPath);

  if (!file.is_open()) {
    throw std::runtime_error(
      "Failed to open: " + outputPath
    );
  }

  file << "player_a,player_b,p_a_wins\n";
  file << std::setprecision(
    std::numeric_limits<double>::max_digits10
  );

  // 自己対戦を除外しつつ、逆順の組み合わせも除外するようなループを回す
  for (std::size_t i = 0; i < parameters.size(); i++) {
    for (std::size_t j = i + 1; j < parameters.size(); j++) {
      const double probability = calculateConditionalWinProbability(
        parameters[i],
        parameters[j],
        performanceStd, sideAdvantage, condition
      );

      file << i << ',' << j << ','
        << probability << '\n';
    }
  }

  file.close();

  if (!file) {
    throw std::runtime_error(
      "Failed to write: " + outputPath
    );
  }
}

namespace project10::io {
namespace {

// 参考記事のgetlineとカンマ分割を、数値だけの共通CSVに適用する。
// 空欄・末尾の余分なカンマも列数検証に含める。引用符付きCSVは対象外。
template <typename Consume>
void readCsv(const std::string& path, const std::string& header, std::size_t columns,
             Consume consume) {
  std::ifstream file(path, std::ios::binary);
  std::string line;
  if (!file) throw std::runtime_error("CSVファイルを開けません: " + path);
  std::size_t lineNumber = 1;
  const auto readLine = [&] {
    if (!std::getline(file, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
  };
  if (!readLine() || line != header) throw std::runtime_error("CSVヘッダーが不正です: " + path);
  while (readLine()) {
    ++lineNumber;
    try {
      std::vector<std::string> fields;
      std::size_t start = 0;
      for (;;) {
        const auto end = line.find(',', start);
        fields.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
      }
      if (fields.size() != columns) throw std::invalid_argument("CSVの列数が正しくありません。");
      consume(fields);
    } catch (const std::exception& error) {
      throw std::runtime_error(path + ":" + std::to_string(lineNumber) + ": " + error.what());
    }
  }
  if (!file.eof()) throw std::runtime_error("CSVファイルを読み込めません: " + path);
}

template <typename Integer = int>
Integer integer(const std::string& cell) {
  Integer value = 0;
  const auto result = std::from_chars(cell.data(), cell.data() + cell.size(), value);
  if (result.ec != std::errc{} || result.ptr != cell.data() + cell.size() || value < 0) {
    throw std::invalid_argument("0以上の整数で指定してください: " + cell);
  }
  return value;
}

double finiteValue(const std::string& cell) {
  std::istringstream input(cell);
  input.imbue(std::locale::classic());
  double value;
  if (!(input >> value) || !(input >> std::ws).eof() ||
      !std::isfinite(value)) {
    throw std::invalid_argument("有限な数値で指定してください: " + cell);
  }
  return value;
}

double probability(const std::string& cell, const std::string& label = "勝率") {
  try {
    const double value = finiteValue(cell);
    if (value < 0.0 || value > 1.0) throw std::invalid_argument("範囲外");
    return value;
  } catch (const std::invalid_argument&) {
    throw std::invalid_argument(label + "は0以上1以下の有限な値で指定してください: " + cell);
  }
}

}  // namespace

std::vector<double> readLatentParametersCsv(const std::string& path, int numPlayers) {
  if (numPlayers < 2) throw std::invalid_argument("プレイヤーは2人以上必要です。");
  std::vector<double> values(numPlayers, std::numeric_limits<double>::quiet_NaN());
  std::size_t count = 0;
  readCsv(path, "player_id,theta_0", 2, [&](const auto& fields) {
    const auto id = static_cast<std::size_t>(integer(fields[0]));
    if (id >= values.size() || std::isfinite(values[id]))
      throw std::invalid_argument("潜在能力のIDが不正か重複しています。");
    values[id] = finiteValue(fields[1]);
    ++count;
  });
  if (count != values.size()) throw std::runtime_error("潜在能力が不足しています: " + path);
  return values;
}

void validatePlayersCsv(const std::string& path, int numPlayers) {
  if (numPlayers < 2) throw std::invalid_argument("プレイヤーは2人以上必要です。");
  std::vector<bool> seen(numPlayers, false);
  std::size_t count = 0;
  readCsv(path, "player_id", 1, [&](const auto& fields) {
    const auto id = static_cast<std::size_t>(integer(fields[0]));
    if (id >= seen.size() || seen[id]) throw std::invalid_argument("未知のプレイヤーID、またはIDの重複があります。");
    seen[id] = true;
    ++count;
  });
  if (count != seen.size()) throw std::runtime_error("プレイヤーIDが不足しています: " + path);
}

std::vector<Match> readMatchesCsv(
  const std::string& path, int numPlayers, int matchesPerPlayer, int period
) {
  if (numPlayers < 2 || numPlayers % 2 != 0 || matchesPerPlayer <= 0 || period <= 0) {
    throw std::invalid_argument("対戦履歴の設定が不正です。");
  }
  std::vector<Match> matches;
  std::vector<std::uint64_t> counts(numPlayers, 0);
  const auto total = static_cast<std::uint64_t>(numPlayers / 2) * matchesPerPlayer;
  readCsv(path, "match_id,period_id,player_a,player_b,result_a", 5, [&](const auto& fields) {
    const Match match{integer<std::uint64_t>(fields[0]), integer(fields[1]),
                      integer(fields[2]), integer(fields[3]), integer(fields[4])};
    if (matches.size() >= total || match.matchID != matches.size() ||
        static_cast<std::uint64_t>(match.periodID) != match.matchID / (numPlayers / 2) / period ||
        match.playerA >= numPlayers || match.playerB >= numPlayers ||
        match.playerA == match.playerB || match.resultA > 1) {
      throw std::invalid_argument("試合ID・期間・プレイヤーID・勝敗が不正です。");
    }
    if (++counts[match.playerA] > static_cast<std::uint64_t>(matchesPerPlayer) ||
        ++counts[match.playerB] > static_cast<std::uint64_t>(matchesPerPlayer)) {
      throw std::invalid_argument("各人の対戦数が設定を超えています。");
    }
    matches.push_back(match);
  });
  if (matches.size() != total) throw std::runtime_error("対戦履歴が不足しています: " + path);
  return matches;
}

evaluation::ProbabilityMatrix readGroundTruthCsv(const std::string& path, int numPlayers) {
  if (numPlayers < 2) throw std::invalid_argument("プレイヤーは2人以上必要です。");
  const auto n = static_cast<std::size_t>(numPlayers);
  if (n > std::numeric_limits<std::size_t>::max() / n) {
    throw std::invalid_argument("プレイヤー数が大きすぎます。");
  }
  evaluation::ProbabilityMatrix truth(n, std::vector<double>(n,
    std::numeric_limits<double>::quiet_NaN()));
  std::size_t count = 0;
  readCsv(path, "player_a,player_b,p_a_wins", 3, [&](const auto& fields) {
    const auto a = static_cast<std::size_t>(integer(fields[0]));
    const auto b = static_cast<std::size_t>(integer(fields[1]));
    if (a >= b || b >= n) throw std::invalid_argument("ペアには既知のIDを使い、player_a < player_bを満たしてください。");
    if (std::isfinite(truth[a][b])) throw std::invalid_argument("正解勝率のペアが重複しています。");
    truth[a][b] = probability(fields[2]);
    truth[b][a] = 1.0 - truth[a][b];
    ++count;
  });
  if (count != n * (n - 1) / 2) throw std::runtime_error("正解勝率のペアが不足しています: " + path);
  return truth;
}

void readPredictionsCsv(const std::string& path, evaluation::MseEvaluator& evaluator) {
  consumePredictionsCsv(path, [&](const auto& row) { evaluator.addPrediction(row); });
}

void consumePredictionsCsv(const std::string& path,
  const std::function<void(const evaluation::PredictionRow&)>& consume) {
  readCsv(path, "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins", 5,
          [&](const auto& fields) {
    consume({integer(fields[0]), integer(fields[1]), integer(fields[2]),
                             integer(fields[3]), probability(fields[4])});
  });
}

std::vector<evaluation::MseResult> readMseCsv(
  const std::string& path, int runID, int matchesPerPlayer, int interval
) {
  if (runID < 0 || interval <= 0 || matchesPerPlayer < interval) {
    throw std::invalid_argument("MSE読み込みの評価設定が不正です。");
  }
  std::map<int, double> points;
  readCsv(path, "run_id,matches_per_player,mse", 3, [&](const auto& fields) {
    if (integer(fields[0]) != runID) throw std::invalid_argument("MSEのrun IDが一致しません。");
    const int checkpoint = integer(fields[1]);
    if (checkpoint <= 0 || checkpoint > matchesPerPlayer || checkpoint % interval != 0) {
      throw std::invalid_argument("MSEの評価時点が設定と一致しません。");
    }
    if (!points.emplace(checkpoint, probability(fields[2], "MSE")).second) {
      throw std::invalid_argument("MSEの評価時点が重複しています。");
    }
  });
  // 全手法を同一の設定上の評価時点へ照合する。欠落を交差集合で隠さない。
  if (points.size() != static_cast<std::size_t>(matchesPerPlayer / interval)) {
    throw std::runtime_error("MSEの評価時点が不足しています。手法間の比較条件が一致しません: " + path);
  }
  std::vector<evaluation::MseResult> results;
  for (const auto& point : points) results.push_back({point.first, point.second});
  return results;
}

namespace {
int side(const std::string& value) {
  if (value != "1" && value != "-1") throw std::invalid_argument("条件zは1または-1が必要です。");
  return value == "1" ? 1 : -1;
}
}

void writeConditions(const std::vector<Match>& matches, const std::string& path) {
  std::ofstream file(path);
  file << "match_id,z\n";
  for (const auto& match : matches) file << match.matchID << ',' << match.condition << '\n';
  file.close();
  if (!file) throw std::runtime_error("条件CSVを保存できません。");
}

void readConditionsCsv(const std::string& path, std::vector<Match>& matches) {
  std::vector<bool> seen(matches.size(), false);
  std::size_t count = 0;
  readCsv(path, "match_id,z", 2, [&](const auto& fields) {
    const auto id = integer<std::uint64_t>(fields[0]);
    if (id >= matches.size() || seen[id]) throw std::invalid_argument("条件の対戦IDが不正か重複しています。");
    matches[id].condition = side(fields[1]); seen[id] = true; ++count;
  });
  if (count != matches.size()) throw std::runtime_error("条件CSVに欠落があります。");
}

void readConditionalPredictionsCsv(const std::string& path,
  evaluation::MseEvaluator& positive, evaluation::MseEvaluator& negative) {
  readCsv(path, "run_id,matches_per_player,player_a,player_b,z,predicted_p_a_wins", 6,
    [&](const auto& fields) {
      const evaluation::PredictionRow row{integer(fields[0]), integer(fields[1]),
        integer(fields[2]), integer(fields[3]), probability(fields[5])};
      (side(fields[4]) == 1 ? positive : negative).addPrediction(row);
    });
}

void readOnlinePredictionsCsv(const std::string& path,
  const std::function<void(const evaluation::OnlinePrediction&)>& consume) {
  readCsv(path, "run_id,match_id,z,result_a,p_elo,p_advantage", 6, [&](const auto& fields) {
    const int result = integer(fields[3]);
    if (result > 1) throw std::invalid_argument("勝敗は0または1が必要です。");
    consume({integer(fields[0]), integer<std::uint64_t>(fields[1]), side(fields[2]),
             result, probability(fields[4]), probability(fields[5])});
  });
}

std::vector<evaluation::OnlineMetrics> readOnlineMetricsCsv(const std::string& path,
  int runID, int matchesPerPlayer, int interval) {
  if (interval <= 0 || matchesPerPlayer < interval) throw std::invalid_argument("評価間隔が不正です。");
  std::map<int, evaluation::OnlineMetrics> points;
  readCsv(path, "run_id,matches_per_player,elo_log_loss,advantage_log_loss,elo_brier,advantage_brier", 6,
    [&](const auto& fields) {
      const int point = integer(fields[1]);
      const double elo = finiteValue(fields[2]), adv = finiteValue(fields[3]);
      if (integer(fields[0]) != runID || point <= 0 || point > matchesPerPlayer ||
          point % interval != 0 || elo < 0 || adv < 0 ||
          !points.emplace(point, evaluation::OnlineMetrics{point, elo, adv,
            probability(fields[4]), probability(fields[5])}).second)
        throw std::invalid_argument("試合前評価のrun ID・時点・値が不正です。");
    });
  if (points.size() != static_cast<std::size_t>(matchesPerPlayer / interval))
    throw std::runtime_error("試合前評価の時点が不足しています。");
  std::vector<evaluation::OnlineMetrics> result;
  for (const auto& point : points) result.push_back(point.second);
  return result;
}

void writeOnlineMetricsCsv(const std::string& path, int runID,
  const std::vector<evaluation::OnlineMetrics>& metrics) {
  if (std::filesystem::exists(std::filesystem::symlink_status(path)))
    throw std::runtime_error("試合前評価の出力先がすでに存在します。");
  std::ofstream file(path);
  file.imbue(std::locale::classic());
  file << "run_id,matches_per_player,elo_log_loss,advantage_log_loss,elo_brier,advantage_brier\n"
       << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto& row : metrics) file << runID << ',' << row.matchesPerPlayer << ',' <<
    row.eloLogLoss << ',' << row.advantageLogLoss << ',' << row.eloBrier << ',' << row.advantageBrier << '\n';
  file.close();
  if (!file) throw std::runtime_error("試合前評価を書き込めません。");
}

void writeMseCsv(
  const std::string& path, int runID, const std::vector<evaluation::MseResult>& results
) {
  if (std::filesystem::exists(std::filesystem::symlink_status(path))) {
    throw std::runtime_error("MSE出力先がすでに存在します: " + path);
  }
  std::ofstream file(path);
  if (!file) throw std::runtime_error("MSE出力ファイルを開けません: " + path);
  file.imbue(std::locale::classic());
  file << "run_id,matches_per_player,mse\n";
  file << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto& result : results) {
    file << runID << ',' << result.matchesPerPlayer << ',' << result.mse << '\n';
  }
  file.close();
  if (!file) throw std::runtime_error("MSE出力ファイルへの書き込みに失敗しました: " + path);
}

void publishMseCsv(const std::string& temporaryPath, const std::string& outputPath) {
  // ハードリンクで存在確認と配置を一体化し、直前に作られた既存ファイルの置換を防ぐ。
  // 非対応環境でも上書きし得る方法へ切り替えず、エラーとして終了する。
  std::error_code error;
  std::filesystem::create_hard_link(temporaryPath, outputPath, error);
  if (error == std::errc::file_exists) {
    throw std::runtime_error("MSE出力先がすでに存在します: " + outputPath);
  }
  if (error) {
    throw std::runtime_error("MSE出力を上書きせずに配置できません（エラーコード: " +
                             std::to_string(error.value()) + "）: " + outputPath);
  }
}

std::vector<double> readGlicko2RatingsCsv(const std::string& path, int numPlayers) {
  std::vector<double> ratings;
  readCsv(path, "rating", 1, [&](const auto& fields) {
    ratings.push_back(finiteValue(fields[0]));
  });
  if (numPlayers < 0 || numPlayers == 1 || ratings.size() < 2 ||
      (numPlayers != 0 && ratings.size() != static_cast<std::size_t>(numPlayers)))
    throw std::runtime_error("Glicko2の最終レート数が人数と一致しません。");
  return ratings;
}

namespace {
template<class Estimator, class Update, class Predict>
void writeOnlinePredictions(
  int runID,
  const std::vector<int>& players,
  const std::vector<Match>& matches,
  int evaluationIntervalMatchesPerPlayer,
  const std::string& outputPath,
  Estimator elo, Update update, Predict predict, bool conditional = false
) {
  if (runID < 0 || evaluationIntervalMatchesPerPlayer <= 0 || players.empty()) {
    throw std::invalid_argument("Invalid arguments for writeEloPredictions");
  }
  if (players.size() % 2 != 0) {
    throw std::invalid_argument("players count must be even");
  }

  // 既存ファイルの保護: 出力先が既に存在する場合は早期にエラーにする
  if (std::filesystem::exists(std::filesystem::symlink_status(outputPath))) {
    throw std::runtime_error("出力先ファイルがすでに存在します: " + outputPath);
  }

  const int totalPlayers = static_cast<int>(players.size());
  const int matchesPerRound = totalPlayers / 2;

  // 排他制御用のロックディレクトリを作成し、その内部の一時ファイルへ書き込む
  const std::filesystem::path lockDir(outputPath + ".lock");
  const RunLock lock(lockDir);
  const std::filesystem::path tmpPath = lockDir / "elo.csv.tmp";

  try {
    std::ofstream file(tmpPath);
    if (!file.is_open()) {
      throw std::runtime_error("一時ファイルを開けません: " + tmpPath.string());
    }

    file.imbue(std::locale::classic());
    file << "run_id,matches_per_player,player_a,player_b,"
         << (conditional ? "z," : "") << "predicted_p_a_wins\n";
    file << std::setprecision(std::numeric_limits<double>::max_digits10);

    for (std::size_t i = 0; i < matches.size(); ++i) {
      update(elo, matches[i]);

      // ラウンド完了の判定
      if ((i + 1) % matchesPerRound == 0) {
        const int currentMatches = static_cast<int>((i + 1) / matchesPerRound);

        if (currentMatches % evaluationIntervalMatchesPerPlayer == 0) {
          // 各人の実際の試合数を確認し、欠損・重複データがないことを検証する
          for (int p = 0; p < totalPlayers; ++p) {
            if (elo.matchCount(p) != currentMatches) {
              throw std::runtime_error("プレイヤーの消化試合数が不揃いです（欠損・重複履歴の検出）");
            }
          }

          // 全ペアの勝率を出力
          for (int a = 0; a < totalPlayers; ++a) {
            for (int b = a + 1; b < totalPlayers; ++b) {
              for (int sideIndex = 0; sideIndex < (conditional ? 2 : 1); ++sideIndex) {
                const int z = conditional ? (sideIndex == 0 ? 1 : -1) : 0;
                file << runID << ','
                     << currentMatches << ','
                     << a << ','
                     << b << ',';
                if (conditional) file << z << ',';
                file << predict(elo, a, b, z) << '\n';
              }
            }
          }
        }
      }
    }

    // 公開前に、端数ラウンドで終わっていないことと全員の最終試合数を検証する
    if (matches.empty() || matches.size() % static_cast<std::size_t>(matchesPerRound) != 0) {
      throw std::runtime_error("対戦履歴がラウンド途中で終わっています");
    }
    const int finalMatches = static_cast<int>(matches.size() / static_cast<std::size_t>(matchesPerRound));
    for (int p = 0; p < totalPlayers; ++p) {
      if (elo.matchCount(p) != finalMatches) {
        throw std::runtime_error("最終的なプレイヤーの消化試合数が不揃いです（欠損・重複履歴の検出）");
      }
    }

    file.close();
    if (!file) {
      throw std::runtime_error("ファイルの書き込みに失敗しました: " + tmpPath.string());
    }

    // ハードリンクで存在確認と公開を一体化（原子的に公開）し、既存ファイルの置換を防ぐ
    std::error_code error;
    std::filesystem::create_hard_link(tmpPath, outputPath, error);
    if (error == std::errc::file_exists) {
      throw std::runtime_error("出力先ファイルがすでに存在します: " + outputPath);
    }
    if (error) {
      throw std::runtime_error("出力ファイルを上書きせずに配置できません（エラーコード: " +
                               std::to_string(error.value()) + "）: " + outputPath);
    }
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(tmpPath, ignored);
    throw;
  }

  std::error_code cleanupError;
  std::filesystem::remove(tmpPath, cleanupError);
}

}  // namespace

void writeEloPredictions(int runID, const std::vector<int>& players,
  const std::vector<Match>& matches, int interval, const std::string& path,
  const ratings::EloParameters& params) {
  writeOnlinePredictions(runID, players, matches, interval, path,
    ratings::EloRating(players, params),
    [](auto& rating, const Match& match) { rating.updateMatch(match); },
    [](const auto& rating, int a, int b, int) { return rating.predictWinProbability(a, b); });
}

void writeAdvantageEloPredictions(int runID, const std::vector<int>& players,
  const std::vector<Match>& matches, int interval, const std::string& path,
  const ratings::AdvantageEloParameters& params, bool conditional, std::ostream* onlineLog) {
  if (onlineLog) *onlineLog << "run_id,match_id,z,result_a,p_elo,p_advantage\n"
    << std::setprecision(std::numeric_limits<double>::max_digits10);
  writeOnlinePredictions(runID, players, matches, interval, path,
    ratings::AdvantageEloRating(players, params),
    [onlineLog, runID, baseline = ratings::EloRating(players,
       {params.initialRating, params.kFactor, params.scale})](auto& rating, const Match& match) mutable {
      if (onlineLog) {
        *onlineLog << runID << ',' << match.matchID << ',' << match.condition << ',' << match.resultA
          << ',' << baseline.predictWinProbability(match.playerA, match.playerB)
          << ',' << rating.predictWinProbability(match.playerA, match.playerB, match.condition) << '\n';
        baseline.updateMatch(match);
      }
      rating.updateMatch(match, match.condition);
    },
    [](const auto& rating, int a, int b, int z) {
      if (z != 0) return rating.predictWinProbability(a, b, z);
      return 0.5 * (rating.predictWinProbability(a, b, 1) +
                    rating.predictWinProbability(a, b, -1));
    }, conditional);
  if (onlineLog && !*onlineLog) throw std::runtime_error("試合前予測ログを書き込めません。");
}

}  // namespace project10::io
