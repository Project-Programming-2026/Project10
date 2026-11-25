#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace project10::io {

// 作業先を削除する前に子プロセスを回収できるよう、破棄時にも終了を待つ。
class ChildProcess {
 public:
  ChildProcess(const std::string& program, const std::vector<std::string>& arguments,
               const std::filesystem::path& directory, const std::filesystem::path& input = {});
  ~ChildProcess();
  void wait();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

// シェルを経由せず起動し、非ゼロ終了も例外にする。標準出力・標準エラーは引き継ぐ。
// program/inputの相対パスは呼び出し元基準。directoryは子の作業先。
void runProcess(const std::string& program, const std::vector<std::string>& arguments,
                const std::filesystem::path& directory,
                const std::filesystem::path& input = {});

}  // namespace project10::io
