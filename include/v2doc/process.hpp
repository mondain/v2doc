#pragma once

#include <string>
#include <vector>

namespace v2doc {

struct ProcessResult {
    int exit_code{};
    std::string stderr_text;
};

class ProcessRunner {
public:
    virtual ~ProcessRunner() = default;

    virtual ProcessResult run(
        const std::vector<std::string> &args) const = 0;
};

class PosixProcessRunner final : public ProcessRunner {
public:
    ProcessResult run(
        const std::vector<std::string> &args) const override;
};

}  // namespace v2doc

