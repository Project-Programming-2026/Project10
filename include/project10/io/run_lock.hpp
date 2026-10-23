#pragma once

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace project10::io {

// 生成と評価で同じロックを使い、読み取り中の入力再生成も防ぐ。
class RunLock {
public:
  explicit RunLock(const std::filesystem::path& path) : path_(path) {
    if (!std::filesystem::create_directory(path_)) {
      throw std::runtime_error("このrunはすでに使用中です: " + path_.string());
    }
  }
  ~RunLock() {
    std::error_code error;
    std::filesystem::remove(path_, error);
    if (error) {
      std::cerr << "runのロック解除に失敗しました: " << path_.string() << '\n';
    }
  }
  RunLock(const RunLock&) = delete;
  RunLock& operator=(const RunLock&) = delete;

private:
  std::filesystem::path path_;
};

}  // namespace project10::io
