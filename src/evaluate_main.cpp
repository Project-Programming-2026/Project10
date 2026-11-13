#include "project10/evaluation/run.hpp"

#include <filesystem>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

int main() {
  try {
    int runID;
    std::string input;
    std::cout << "Enter run ID: ";
    std::getline(std::cin, input);
    std::istringstream parser(input);
    if (!(parser >> runID) || runID < 0 || !(parser >> std::ws).eof()) {
      throw std::invalid_argument("run IDが不正です。0以上の整数を入力してください。");
    }
    std::cout << "Enter algorithm (Elo/Glicko/Glicko2/TrueSkill/AdvantageElo/rankedGlicko2): ";
    std::string algorithm;
    std::getline(std::cin, algorithm);
    const auto result = project10::evaluation::evaluateRun(runID, algorithm);
    if (!result.cleanupWarning.empty()) {
      std::cerr << "警告: MSEの出力は完了しましたが、一時ファイルを削除できません: "
                << result.cleanupWarning.string() << '\n';
    }
    std::cout << "MSE output: " << result.output.string() << '\n';
  } catch (const fs::filesystem_error& error) {
    std::cerr << "エラー: ファイル操作に失敗しました（エラーコード: " << error.code().value()
              << "）: " << error.path1().string() << '\n';
    return 1;
  } catch (const std::bad_alloc&) {
    std::cerr << "エラー: MSE評価に必要なメモリを確保できません。\n";
    return 1;
  } catch (const std::length_error&) {
    std::cerr << "エラー: 評価データが大きすぎて処理できません。\n";
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
