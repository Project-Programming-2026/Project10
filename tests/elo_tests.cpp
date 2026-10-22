#include "project10/ratings/elo.hpp"
#include "project10/io/csv.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Action>
void rejects(Action action) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("std::invalid_argumentが送出されませんでした");
}

void testEloWinProbability() {
  constexpr double tolerance = 1e-10;

  // 1. 同レートの勝率はちょうど 0.5
  require(project10::ratings::calculateEloWinProbability(1500.0, 1500.0) == 0.5,
          "Equal ratings must give 0.5 probability");

  // 2. レート差400のとき: 1 / (1 + 10^(-400/400)) = 10 / 11
  const double pHighLow = project10::ratings::calculateEloWinProbability(1900.0, 1500.0);
  require(std::abs(pHighLow - (10.0 / 11.0)) < tolerance,
          "Rating difference of 400 must match 10/11");

  const double pLowHigh = project10::ratings::calculateEloWinProbability(1500.0, 1900.0);
  require(std::abs(pLowHigh - (1.0 / 11.0)) < tolerance,
          "Rating difference of -400 must match 1/11");

  // 3. 対称性: P(A wins) + P(B wins) == 1.0
  require(std::abs(pHighLow + pLowHigh - 1.0) < tolerance,
          "Swapped probabilities must sum to 1.0");

  // 4. 極端なレート差（オーバーフロー耐性とクリッピング）
  // 10^300を超える差では安全に 1.0 または 0.0 にクリッピングされる
  const double extremeWin = project10::ratings::calculateEloWinProbability(200000.0, 0.0);
  require(extremeWin == 1.0, "Extreme positive rating difference must clip to 1.0");

  const double extremeLoss = project10::ratings::calculateEloWinProbability(0.0, 200000.0);
  require(extremeLoss == 0.0, "Extreme negative rating difference must clip to 0.0");

  // 一般的な大差（10000点差）でも有限値かつ0〜1の範囲に収まる
  const double largeWin = project10::ratings::calculateEloWinProbability(10000.0, 0.0);
  require(largeWin > 1.0 - 1e-15 && largeWin <= 1.0, "Large win must be near 1.0");

  const double largeLoss = project10::ratings::calculateEloWinProbability(0.0, 10000.0);
  require(largeLoss >= 0.0 && largeLoss < 1e-15, "Large loss must be near 0.0");

  // 5. 不正な入力の拒否
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double inf = std::numeric_limits<double>::infinity();

  rejects([&] { project10::ratings::calculateEloWinProbability(1500.0, 1500.0, 0.0); });
  rejects([&] { project10::ratings::calculateEloWinProbability(1500.0, 1500.0, -100.0); });
  rejects([&] { project10::ratings::calculateEloWinProbability(1500.0, 1500.0, nan); });
  rejects([&] { project10::ratings::calculateEloWinProbability(1500.0, 1500.0, inf); });
  rejects([&] { project10::ratings::calculateEloWinProbability(nan, 1500.0); });
  rejects([&] { project10::ratings::calculateEloWinProbability(1500.0, inf); });
}

void testEloParameters() {
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double inf = std::numeric_limits<double>::infinity();

  // initialRating
  rejects([&] {
    project10::ratings::EloParameters p; p.initialRating = nan;
    project10::ratings::EloRating elo(2, p);
  });
  rejects([&] {
    project10::ratings::EloParameters p; p.initialRating = inf;
    project10::ratings::EloRating elo(2, p);
  });

  // kFactor
  rejects([&] {
    project10::ratings::EloParameters p; p.kFactor = 0.0;
    project10::ratings::EloRating elo(2, p);
  });
  rejects([&] {
    project10::ratings::EloParameters p; p.kFactor = -32.0;
    project10::ratings::EloRating elo(2, p);
  });
  rejects([&] {
    project10::ratings::EloParameters p; p.kFactor = nan;
    project10::ratings::EloRating elo(2, p);
  });
  rejects([&] {
    project10::ratings::EloParameters p; p.kFactor = inf;
    project10::ratings::EloRating elo(2, p);
  });

  // scale
  rejects([&] {
    project10::ratings::EloParameters p; p.scale = 0.0;
    project10::ratings::EloRating elo(2, p);
  });
  rejects([&] {
    project10::ratings::EloParameters p; p.scale = -400.0;
    project10::ratings::EloRating elo(2, p);
  });
  rejects([&] {
    project10::ratings::EloParameters p; p.scale = nan;
    project10::ratings::EloRating elo(2, p);
  });
  rejects([&] {
    project10::ratings::EloParameters p; p.scale = inf;
    project10::ratings::EloRating elo(2, p);
  });
}

void testEloRatingUpdate() {
  constexpr double tolerance = 1e-10;

  project10::ratings::EloParameters params{1500.0, 32.0, 400.0};
  project10::ratings::EloRating elo(2, params);

  require(elo.numPlayers() == 2, "numPlayers must be 2");
  require(elo.rating(0) == 1500.0, "Initial rating must be 1500");
  require(elo.rating(1) == 1500.0, "Initial rating must be 1500");
  require(elo.matchCount(0) == 0, "Initial match count must be 0");

  // 1試合目: player 0 が勝つ (resultA = 1)
  // expected = 0.5, delta = 32 * (1.0 - 0.5) = +16.0
  elo.updateMatch(0, 1, 1);

  require(std::abs(elo.rating(0) - 1516.0) < tolerance, "Winner rating must increase by 16");
  require(std::abs(elo.rating(1) - 1484.0) < tolerance, "Loser rating must decrease by 16");
  require(elo.matchCount(0) == 1, "Match count must be 1");
  require(elo.matchCount(1) == 1, "Match count must be 1");

  // 不正な操作の拒否
  rejects([&] { elo.updateMatch(0, 0, 1); });  // 自己対戦
  rejects([&] { elo.updateMatch(0, 1, 2); });  // 不正な勝敗コード
  rejects([&] { elo.rating(5); });             // 未知のプレイヤー
}

void testPredictionsOutputProtection() {
  const std::string testFile = "test_elo_protection.csv";
  std::filesystem::remove(testFile);
  std::filesystem::remove(testFile + ".tmp");

  const std::vector<int> players = {0, 1};
  const std::vector<Match> matches = {
    {0, 0, 0, 1, 1}
  };

  // 正常出力
  project10::io::writeEloPredictions(0, players, matches, 1, testFile);
  require(std::filesystem::exists(testFile), "Output file must exist after writing");

  // 既存ファイルの保護確認（2回目の書き込みは拒否される）
  bool caughtExisting = false;
  try {
    project10::io::writeEloPredictions(0, players, matches, 1, testFile);
  } catch (const std::runtime_error&) {
    caughtExisting = true;
  }
  require(caughtExisting, "Must throw runtime_error when output file already exists");

  std::filesystem::remove(testFile);
}

}  // namespace

int main() {
  try {
    testEloWinProbability();
    testEloParameters();
    testEloRatingUpdate();
    testPredictionsOutputProtection();
    std::cout << "Elo単体確認に成功しました" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "テスト失敗: " << error.what() << std::endl;
    return 1;
  }
}
