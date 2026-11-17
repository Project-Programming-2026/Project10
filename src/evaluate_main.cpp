#include "project10/evaluation/run.hpp"
#include "project10/evaluation/files.hpp"

#include <filesystem>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  try {
    const auto options = project10::evaluation::parseFileOptions(argc, argv, false);
    if (options.help) {
      std::cout << "project10_evaluate [--ranking]\n"
                << "  引数なし: run IDと手法名を標準入力から読みます。--rankingで順位評価を行います。\n"
                << "project10_evaluate --input label run_ID CSV [--input ...] --name name [--ranking]\n"
                << "  任意の予測CSVを評価し、runs/comparisons/name/へ保存します。上書きしません。\n";
      return 0;
    }
    if (!options.inputs.empty()) {
      const auto output = project10::evaluation::evaluateFiles(
        options.inputs, options.name, options.arguments, options.ranking);
      std::cout << (options.ranking ? "Ranking output: " : "MSE output: ") << output.string() << '\n';
      return 0;
    }
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
    const auto result = options.ranking
      ? project10::evaluation::evaluateRunRanking(runID, algorithm)
      : project10::evaluation::evaluateRun(runID, algorithm);
    if (!result.cleanupWarning.empty()) {
      std::cerr << "警告: " << (options.ranking ? "順位評価" : "MSE")
                << "の出力は完了しましたが、一時ファイルを削除できません: "
                << result.cleanupWarning.string() << '\n';
    }
    std::cout << (options.ranking ? "Ranking output: " : "MSE output: ")
              << result.output.string() << '\n';
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
