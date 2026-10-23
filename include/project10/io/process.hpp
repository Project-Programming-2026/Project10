#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace project10::io {

// シェルを経由せず起動し、非ゼロ終了も例外にする。標準出力・標準エラーは引き継ぐ。
// program/inputの相対パスは呼び出し元基準。directoryは子の作業先。
void runProcess(const std::string& program, const std::vector<std::string>& arguments,
                const std::filesystem::path& directory,
                const std::filesystem::path& input = {});

}  // namespace project10::io
