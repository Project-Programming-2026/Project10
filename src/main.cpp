#include "project10/simulation/run.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// 1回の実行で、指定したrun IDの設定から1試行分のデータを生成・保存する。
int main() {
  // run IDを1行で受け取る。設定ファイルの選択はloadConfigに任せる。
  int runID;
  std::string input;
  std::cout << "Enter run ID: ";
  std::getline(std::cin, input);
  std::istringstream runInput(input);

  // 整数への変換・非負の条件を確認し、末尾の空白以外に文字があれば拒否する。
  if (!(runInput >> runID) || runID < 0 || !(runInput >> std::ws).eof()) {
    std::cerr << "Invalid run ID." << std::endl;
    return 1;
  }

  try {
    const auto result = project10::simulation::generateRun(runID, [](const fs::path& outputDir) {
      std::cout << "Output directory already exists: " << outputDir.string() << '\n';
      std::cout << "Overwrite config.json and the 4 generated CSV files? [y/N]: ";
      std::string confirmation;
      return std::getline(std::cin, confirmation) &&
             (confirmation == "y" || confirmation == "Y");
    });
    if (!result) {
      std::cerr << "Overwrite cancelled." << std::endl;
      return 1;
    }

    // すべての保存が成功してから、実行した試行と出力先を表示する。
    std::cout << "Run ID: " << runID << std::endl;
    std::cout << "Number of players: " << result->numPlayers << std::endl;
    std::cout << "Output: " << result->outputDir.string() << std::endl;
  } catch (const std::exception& e) {
    // 設定・生成・保存の失敗を表示し、終了コード1で呼び出し側にも知らせる。
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
