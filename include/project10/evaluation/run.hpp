#pragma once

#include "project10/io/run_lock.hpp"
#include "project10/evaluation/mse.hpp"

#include <filesystem>
#include <string>

namespace project10::evaluation {

struct EvaluationResult {
  std::filesystem::path output;
  // 公開成功後の一時ファイル削除失敗だけを通知する。通常は空。
  std::filesystem::path cleanupWarning;
};

// カレントディレクトリのruns/内の保存済み設定・予測を使用する。
// 既定では自身でrunロックを取得し、入力検証からMSE公開・一時ファイル削除まで保持する。
// 既存MSEは上書きしない。失敗は例外、表示は呼び出し側の責務。
// borrowedLock指定時は対象runとの一致を検査し、取得・解放は呼び出し側に任せる。
EvaluationResult evaluateRun(int runID, const std::string& algorithm,
                             const io::RunLock* borrowedLock = nullptr);
// CSVの検証・MSE計算だけを行い、既存結果を変更しない。
std::vector<MseResult> calculateRunMse(int runID, const std::string& algorithm,
                                      const io::RunLock* borrowedLock = nullptr);

}  // namespace project10::evaluation
