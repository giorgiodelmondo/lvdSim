// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lvd::tools {

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Quoting rules of CommandLineToArgvW / the MSVC runtime.
std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < a.size() && a[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == a.size()) {
            r.append(backslashes * 2, L'\\');
            break;
        }
        if (a[i] == L'"') {
            r.append(backslashes * 2 + 1, L'\\');
            r += L'"';
        } else {
            r.append(backslashes, L'\\');
            r += a[i];
        }
    }
    r += L'"';
    return r;
}

std::wstring getEnv(const wchar_t* name, bool* present) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) {
        *present = false;
        return {};
    }
    std::wstring v(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(got);
    *present = true;
    return v;
}

// Sets variables for the lifetime of the object and restores the previous values afterwards.
class EnvGuard {
public:
    void set(const std::wstring& name, const std::wstring& value) {
        Saved s;
        s.name = name;
        s.old = getEnv(name.c_str(), &s.had);
        saved_.push_back(s);
        SetEnvironmentVariableW(name.c_str(), value.c_str());
    }
    ~EnvGuard() {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it)
            SetEnvironmentVariableW(it->name.c_str(), it->had ? it->old.c_str() : nullptr);
    }

private:
    struct Saved {
        std::wstring name, old;
        bool had = false;
    };
    std::vector<Saved> saved_;
};

}  // namespace

ProcessResult runProcess(const std::vector<std::string>& argv, const ProcessOptions& options) {
    ProcessResult result;
    if (argv.empty()) {
        result.error = "empty command";
        return result;
    }

    EnvGuard guard;
    for (const auto& [k, v] : options.env) guard.set(widen(k), widen(v));
    if (!options.pathPrepend.empty()) {
        bool had = false;
        const std::wstring old = getEnv(L"PATH", &had);
        std::wstring path;
        for (const std::string& d : options.pathPrepend) path += widen(d) + L";";
        guard.set(L"PATH", path + old);
    }

    std::wstring cmd;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) cmd += L' ';
        cmd += quoteArg(widen(argv[i]));
    }

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (options.capture) {
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
            result.error = "CreatePipe failed";
            return result;
        }
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
        si.hStdOutput = writeEnd;
        si.hStdError = writeEnd;
    }

    PROCESS_INFORMATION pi{};
    const std::wstring cwd = widen(options.workingDir);
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                   cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (writeEnd != nullptr) CloseHandle(writeEnd);
    if (!ok) {
        if (readEnd != nullptr) CloseHandle(readEnd);
        result.error = "cannot start '" + argv[0] + "' (error " + std::to_string(GetLastError()) + ")";
        return result;
    }
    result.started = true;

    if (readEnd != nullptr) {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(readEnd, buf, sizeof buf, &n, nullptr) && n > 0) result.output.append(buf, n);
        CloseHandle(readEnd);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exitCode = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

std::string executablePath() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
    if (n == 0) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), out.data(), len, nullptr, nullptr);
    return out;
}

#else  // POSIX

ProcessResult runProcess(const std::vector<std::string>& argv, const ProcessOptions& options) {
    ProcessResult result;
    if (argv.empty()) {
        result.error = "empty command";
        return result;
    }

    int fds[2] = {-1, -1};
    if (options.capture && pipe(fds) != 0) {
        result.error = "pipe failed";
        return result;
    }

    int errPipe[2];
    if (pipe(errPipe) != 0) {
        result.error = "pipe failed";
        return result;
    }
    fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = fork();
    if (pid < 0) {
        result.error = "fork failed";
        return result;
    }
    if (pid == 0) {
        if (options.capture) {
            dup2(fds[1], STDOUT_FILENO);
            dup2(fds[1], STDERR_FILENO);
            close(fds[0]);
            close(fds[1]);
        }
        if (!options.workingDir.empty() && chdir(options.workingDir.c_str()) != 0) _exit(126);
        for (const auto& [k, v] : options.env) setenv(k.c_str(), v.c_str(), 1);
        if (!options.pathPrepend.empty()) {
            std::string path;
            for (const std::string& d : options.pathPrepend) path += d + ":";
            if (const char* old = getenv("PATH")) path += old;
            setenv("PATH", path.c_str(), 1);
        }
        std::vector<char*> args;
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        const char c = 1;
        (void)!write(errPipe[1], &c, 1);  // tell the parent that exec failed
        _exit(127);
    }

    close(errPipe[1]);
    if (options.capture) {
        close(fds[1]);
        char buf[4096];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof buf)) > 0) result.output.append(buf, static_cast<std::size_t>(n));
        close(fds[0]);
    }
    char flag;
    const bool execFailed = read(errPipe[0], &flag, 1) == 1;
    close(errPipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (execFailed) {
        result.error = "cannot start '" + argv[0] + "'";
        return result;
    }
    result.started = true;
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return result;
}

std::string executablePath() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return {};
    return std::string(buf, static_cast<std::size_t>(n));
}

#endif

}  // namespace lvd::tools
