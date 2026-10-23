#pragma once

#include "project10/io/run_lock.hpp"

#include <filesystem>
#include <functional>
#include <optional>

namespace project10::simulation {

struct GenerationResult {
  int numPlayers;
  std::filesystem::path outputDir;
};

// 既存ディレクトリについて、ロック保持中に一度だけ問い合わせる。
// 対話は呼び出し側の責務。未指定・falseなら既存ファイルを変更せずnulloptを返す。
using ConfirmOverwrite = std::function<bool(const std::filesystem::path&)>;

// カレントディレクトリのconfigs/から読み、runs/へ保存する。
// 既定では自身でrunロックを取得・解放し、呼び出し側で同じロックを取らない。
// 失敗は例外で通知。上書き承認後も全出力先を検査してから書き始める。
// borrowedLock指定時は対象runとの一致を検査し、取得・解放は呼び出し側に任せる。
std::optional<GenerationResult> generateRun(int runID, const ConfirmOverwrite& confirm = {},
                                          const io::RunLock* borrowedLock = nullptr);

}  // namespace project10::simulation
