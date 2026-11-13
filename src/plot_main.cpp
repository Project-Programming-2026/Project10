#include "project10/evaluation/plot.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    const auto options = project10::evaluation::parseFileOptions(argc, argv, true);
    if (options.help) {
      std::cout << "project10_plot [--png] [--name graphs] [--gnuplot executable]\n"
                << "run IDを標準入力から読みます。既存出力は上書きしません。\n"
                << "project10_plot --input label run_ID CSV [--input ...] --name name [--png] [--gnuplot executable]\n"
                << "任意のMSE CSVを描画し、runs/comparisons/name/へ保存します。\n";
      return 0;
    }
    if (!options.inputs.empty()) {
      const auto output = project10::evaluation::plotFiles(options.inputs, options.name,
        options.png, options.gnuplot, options.arguments);
      std::cout << "グラフ保存先: " << output.string() << '\n';
      return 0;
    }
    std::cout << "Enter run ID: ";
    std::string input;
    std::getline(std::cin, input);
    std::istringstream parser(input);
    int runID;
    if (!(parser >> runID) || runID < 0 || !(parser >> std::ws).eof()) {
      throw std::invalid_argument("run IDが不正です。0以上の整数を入力してください。");
    }
    project10::evaluation::plotRun(runID, options.png, options.name.empty() ? "graphs" : options.name,
                                 options.gnuplot, std::cout);
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
}
