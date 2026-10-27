#pragma once

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace project10::io {

// 生成と評価で同じロックを使い、読み取り中の入力再生成も防ぐ。
class RunLock {
public:
  explicit RunLock(const std::filesystem::path& path)
    : path_(path), absolutePath_(std::filesystem::absolute(path).lexically_normal()) {
    if (!std::filesystem::create_directory(path_)) {
      throw std::runtime_error("このrunはすでに使用中です: " + path_.string());
    }
  }
  ~RunLock() {
    std::error_code error;
    std::filesystem::remove(absolutePath_, error);
    if (error) {
      std::cerr << "runのロック解除に失敗しました: " << path_.string() << '\n';
    }
  }
  RunLock(const RunLock&) = delete;
  RunLock& operator=(const RunLock&) = delete;

  // 統括側が保持するロックを借りる場合も、別runのロックによる検査の迂回を防ぐ。
  void requirePath(const std::filesystem::path& path) const {
    if (absolutePath_ != std::filesystem::absolute(path).lexically_normal()) {
      throw std::invalid_argument("対象runのロックが必要です。");
    }
  }

private:
  std::filesystem::path path_;
  std::filesystem::path absolutePath_;
};

}  // namespace project10::io
