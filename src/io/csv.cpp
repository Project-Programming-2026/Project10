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
  const std::string& outputPath
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
      const double probability = calculateWinProbability(
        parameters[i],
        parameters[j],
        performanceStd
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

int integer(const std::string& cell) {
  int value = 0;
  const auto result = std::from_chars(cell.data(), cell.data() + cell.size(), value);
  if (result.ec != std::errc{} || result.ptr != cell.data() + cell.size() || value < 0) {
    throw std::invalid_argument("0以上の整数で指定してください: " + cell);
  }
  return value;
}

double probability(const std::string& cell, const std::string& label = "勝率") {
  std::istringstream input(cell);
  input.imbue(std::locale::classic());
  double value;
  if (!(input >> value) || !(input >> std::ws).eof() ||
      !std::isfinite(value) || value < 0.0 || value > 1.0) {
    throw std::invalid_argument(label + "は0以上1以下の有限な値で指定してください: " + cell);
  }
  return value;
}

}  // namespace

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
  readCsv(path, "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins", 5,
          [&](const auto& fields) {
    evaluator.addPrediction({integer(fields[0]), integer(fields[1]), integer(fields[2]),
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

void writeEloPredictions(
  int runID,
  const std::vector<int>& players,
  const std::vector<Match>& matches,
  int evaluationIntervalMatchesPerPlayer,
  const std::string& outputPath,
  const ratings::EloParameters& params
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

  // Elo計算機を用意
  ratings::EloRating elo(players, params);

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
    file << "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n";
    file << std::setprecision(std::numeric_limits<double>::max_digits10);

    for (std::size_t i = 0; i < matches.size(); ++i) {
      elo.updateMatch(matches[i]);

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
              file << runID << ','
                   << currentMatches << ','
                   << a << ','
                   << b << ','
                   << elo.predictWinProbability(a, b) << '\n';
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

}  // namespace project10::io
