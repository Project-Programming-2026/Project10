#include "project10/io/config.hpp"
#include "project10/io/csv.hpp"
#include "project10/simulation/latent_parameters.hpp"
#include "project10/simulation/players.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
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
    // configs/run_XXXX/config.jsonを読み込み、IDの一致・型・値の範囲を検証する。
    const auto config = project10::io::loadConfig(runID);

    // プレイヤーIDは0からの連番、潜在能力は設定した正規分布とシードから生成する。
    // 出力先を作る前に生成を終え、生成失敗による空の結果ディレクトリを避ける。
    const auto players = generatePlayers(config.numPlayers);
    const auto parameters = generateLatentParameters(
      config.numPlayers, config.mean, config.stddev, config.abilitySeed
    );

    // run IDを最低4桁にゼロ埋めし、この試行の結果をruns/run_XXXX/へまとめる。
    const auto outputDir = fs::path("runs") / project10::io::runDirectoryName(runID);
    fs::create_directories("runs");

    // 作成できた実行だけが書き込みへ進む。既存の空ディレクトリも上書きしない。
    // 事前の存在確認と作成を分けず、同時実行による競合も防ぐ。
    if (!fs::create_directory(outputDir)) {
      throw std::runtime_error("Output directory already exists: " + outputDir.string());
    }

    // IDのみのplayers.csvと、ID・潜在能力を持つlatent_parameters.csvを別々に保存する。
    // 潜在能力は生成・評価用の正解情報であり、レーティング手法への入力には使わない。

    writePlayers(players, (outputDir / config.playersFile).string());
    writeLatentParameters(parameters, (outputDir / config.latentParametersFile).string());

    // 読み込み時点の設定全体を保存し、結果と使用した条件・シードを対応づける。
    project10::io::writeConfigSnapshot(config, outputDir);

    // すべての保存が成功してから、実行した試行と出力先を表示する。
    std::cout << "Run ID: " << runID << std::endl;
    std::cout << "Number of players: " << config.numPlayers << std::endl;
    std::cout << "Output: " << outputDir.string() << std::endl;
  } catch (const std::exception& e) {
    // 設定・生成・保存の失敗を表示し、終了コード1で呼び出し側にも知らせる。
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
