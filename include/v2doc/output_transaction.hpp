#pragma once

#include <filesystem>

namespace v2doc {

bool is_v2doc_report(const std::filesystem::path &directory);

class OutputTransaction {
public:
    OutputTransaction(std::filesystem::path target, bool force);
    ~OutputTransaction();

    OutputTransaction(const OutputTransaction &) = delete;
    OutputTransaction &operator=(const OutputTransaction &) = delete;
    OutputTransaction(OutputTransaction &&) = delete;
    OutputTransaction &operator=(OutputTransaction &&) = delete;

    const std::filesystem::path &staging_path() const;
    void commit();

private:
    std::filesystem::path target_;
    std::filesystem::path staging_;
    bool force_{};
    bool committed_{};
};

}  // namespace v2doc

