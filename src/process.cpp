#include "v2doc/process.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace v2doc {
namespace {

constexpr std::size_t maximum_stderr_bytes = 1024U * 1024U;

void close_descriptor(const int descriptor) {
    if (descriptor >= 0) {
        while (::close(descriptor) < 0 && errno == EINTR) {
        }
    }
}

}  // namespace

ProcessResult PosixProcessRunner::run(
    const std::vector<std::string> &args) const {
    if (args.empty()) {
        throw std::invalid_argument("process argument vector is empty");
    }

    std::array<int, 2> stderr_pipe{-1, -1};
    if (::pipe2(stderr_pipe.data(), O_CLOEXEC) != 0) {
        throw std::runtime_error(
            "pipe2 failed: " + std::string{std::strerror(errno)});
    }

    const pid_t child = ::fork();
    if (child < 0) {
        const auto error = errno;
        close_descriptor(stderr_pipe[0]);
        close_descriptor(stderr_pipe[1]);
        throw std::runtime_error(
            "fork failed: " + std::string{std::strerror(error)});
    }

    if (child == 0) {
        close_descriptor(stderr_pipe[0]);
        if (::dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            constexpr char message[] = "dup2 failed\n";
            const auto write_result =
                ::write(STDERR_FILENO, message, sizeof(message) - 1U);
            static_cast<void>(write_result);
            _exit(126);
        }
        close_descriptor(stderr_pipe[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1U);
        for (const auto &argument : args) {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());

        constexpr char message[] = "execvp failed\n";
        const auto write_result =
            ::write(STDERR_FILENO, message, sizeof(message) - 1U);
        static_cast<void>(write_result);
        _exit(127);
    }

    close_descriptor(stderr_pipe[1]);
    std::string stderr_text;
    std::array<char, 4096> buffer{};
    while (true) {
        const auto count =
            ::read(stderr_pipe[0], buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            close_descriptor(stderr_pipe[0]);
            static_cast<void>(::waitpid(child, nullptr, 0));
            throw std::runtime_error(
                "read failed: " + std::string{std::strerror(errno)});
        }
        if (stderr_text.size() < maximum_stderr_bytes) {
            const auto available =
                maximum_stderr_bytes - stderr_text.size();
            const auto append_count = std::min(
                available, static_cast<std::size_t>(count));
            stderr_text.append(buffer.data(), append_count);
        }
    }
    close_descriptor(stderr_pipe[0]);

    int status{};
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error(
                "waitpid failed: " + std::string{std::strerror(errno)});
        }
    }

    if (WIFEXITED(status)) {
        return {WEXITSTATUS(status), std::move(stderr_text)};
    }
    if (WIFSIGNALED(status)) {
        return {128 + WTERMSIG(status), std::move(stderr_text)};
    }
    return {1, std::move(stderr_text)};
}

}  // namespace v2doc
