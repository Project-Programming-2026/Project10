#include "project10/io/csv.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("専用のテスト作業先が必要です。");
    const auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    const auto path = (directory / "fixture.csv").string();
    const auto write = [&](const std::string& content) {
      std::ofstream file(path, std::ios::binary);
      file << content;
    };
    const std::string header = "run_id,matches_per_player,mse\n";
    // 架空値はこのテストの専用領域だけに保存する。
    write("run_id,matches_per_player,mse\r\n7,20,0\r\n7,10,0.0625\r\n");
    const auto points = project10::io::readMseCsv(path, 7, 25, 10);
    if (points.size() != 2 || points[0].matchesPerPlayer != 10 ||
        points[0].mse != 0.0625 || points[1].matchesPerPlayer != 20 || points[1].mse != 0) {
      throw std::runtime_error("MSEの値・昇順・端数処理が一致しません。");
    }
    const auto reject = [&](const std::string& data) {
      write(data);
      try { project10::io::readMseCsv(path, 7, 25, 10); }
      catch (const std::exception&) { return; }
      throw std::runtime_error("不正CSVを受理しました: " + data);
    };
    for (const auto* value : {"NaN", "Inf", "-Inf", "1e999", "-0.1", "1.1", "", "0.1x"}) {
      reject(header + "7,10," + value + "\n7,20,0\n");
    }
    for (const auto* row : {"8,10,0.1", "-1,10,0.1", "7,0,0.1", "7,15,0.1",
                           "7,30,0.1", "7,10.0,0.1", "7,2147483648,0.1",
                           "7,10,0.1,", "7,10", "", "7,10,\"0.1\""}) {
      reject(header + row + "\n7,20,0\n");
    }
    reject(header);
    reject(header + "7,10,0.1\n");
    reject(header + "7,10,0.1\n7,10,0.1\n7,20,0\n");
    reject("run_id,matches_per_player,error\n7,10,0.1\n7,20,0\n");
    reject("");
    write(header + "7,10,1\n7,20,0\n");
    project10::io::readMseCsv(path, 7, 25, 10);
    std::cout << "MSE描画入力の正常系・異常系を確認しました。\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
