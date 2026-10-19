#include "project10/io/csv.hpp"
#include "project10/simulation/probit.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <locale>
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

double probability(const std::string& cell) {
  std::istringstream input(cell);
  input.imbue(std::locale::classic());
  double value;
  if (!(input >> value) || !(input >> std::ws).eof() ||
      !std::isfinite(value) || value < 0.0 || value > 1.0) {
    throw std::invalid_argument("勝率は0以上1以下の有限な値で指定してください: " + cell);
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

}  // namespace project10::io
