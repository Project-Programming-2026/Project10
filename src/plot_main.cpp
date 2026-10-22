#include "project10/evaluation/plot.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    bool png = false;
    std::string name = "graphs", gnuplot = "gnuplot";
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--png") png = true;
      else if ((option == "--name" || option == "--gnuplot") && i + 1 < argc) {
        (option == "--name" ? name : gnuplot) = argv[++i];
      } else if (option == "--help") {
        std::cout << "project10_plot [--png] [--name graphs] [--gnuplot executable]\n"
                  << "run IDを標準入力から読みます。既存出力は上書きしません。\n";
        return 0;
      } else throw std::invalid_argument("不正なオプションです: " + option);
    }
    std::cout << "Enter run ID: ";
    std::string input;
    std::getline(std::cin, input);
    std::istringstream parser(input);
    int runID;
    if (!(parser >> runID) || runID < 0 || !(parser >> std::ws).eof()) {
      throw std::invalid_argument("run IDが不正です。0以上の整数を入力してください。");
    }
    project10::evaluation::plotRun(runID, png, name, gnuplot, std::cout);
  } catch (const std::exception& error) {
    std::cerr << "エラー: " << error.what() << '\n';
    return 1;
  }
}
