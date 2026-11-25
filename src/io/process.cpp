#include "project10/io/process.hpp"

#include <cerrno>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace project10::io {

struct ChildProcess::State {
  std::string program;
#ifdef _WIN32
  HANDLE child = nullptr;
#else
  pid_t child = -1;
#endif
  bool wait() noexcept {
#ifdef _WIN32
    if (!child) return true;
    const auto waited = WaitForSingleObject(child, INFINITE);
    DWORD status = 1;
    const bool obtained = GetExitCodeProcess(child, &status) != 0;
    CloseHandle(child);
    child = nullptr;
    return waited == WAIT_OBJECT_0 && obtained && status == 0;
#else
    if (child < 0) return true;
    int status = 0;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result == -1 && errno == EINTR);
    child = -1;
    return result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
  }
};

ChildProcess::ChildProcess(const std::string& program, const std::vector<std::string>& arguments,
                           const std::filesystem::path& directory, const std::filesystem::path& input)
    : state_(new State{program}) {
  auto executable = std::filesystem::path(program);
  if (executable.has_parent_path()) executable = std::filesystem::absolute(executable);
#ifdef _WIN32
  // Windowsのargv復元規則に従い、引用符の直前と末尾のバックスラッシュを二重化する。
  auto quote = [](const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : value) {
      if (c == L'\\') { ++slashes; continue; }
      result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
      if (c == L'"') result += L'\\';
      result += c;
      slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
  };
  std::wstring command = quote(executable.wstring());
  for (const auto& argument : arguments) command += L" " + quote(std::filesystem::path(argument).wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  HANDLE source = INVALID_HANDLE_VALUE;
  if (!input.empty()) {
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    source = CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, &attributes,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (source == INVALID_HANDLE_VALUE) throw std::runtime_error("標準入力ファイルを開けません。");
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = source;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }
  PROCESS_INFORMATION process{};
  const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, !input.empty(),
    CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process) != 0;
  if (source != INVALID_HANDLE_VALUE) CloseHandle(source);
  if (!started) throw std::runtime_error("外部プログラムを起動できません: " + program);
  CloseHandle(process.hThread);
  state_->child = process.hProcess;
#else
  std::vector<std::string> values{executable.string()};
  values.insert(values.end(), arguments.begin(), arguments.end());
  std::vector<char*> argv;
  for (auto& value : values) argv.push_back(value.data());
  argv.push_back(nullptr);
  const auto child = fork();
  if (child < 0) throw std::runtime_error("外部プロセスを作成できません。");
  if (child == 0) {
    if (!input.empty()) {
      const int source = open(input.c_str(), O_RDONLY);
      if (source < 0 || dup2(source, STDIN_FILENO) < 0) _exit(126);
      if (source != STDIN_FILENO) close(source);
    }
    if (chdir(directory.c_str()) != 0) _exit(126);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  state_->child = child;
#endif
}

ChildProcess::~ChildProcess() {
  state_->wait();
}

void ChildProcess::wait() {
  if (!state_->wait())
    throw std::runtime_error("外部プログラムの実行に失敗しました: " + state_->program);
}

void runProcess(const std::string& program, const std::vector<std::string>& arguments,
                const std::filesystem::path& directory, const std::filesystem::path& input) {
  ChildProcess child(program, arguments, directory, input);
  child.wait();
}

}  // namespace project10::io
