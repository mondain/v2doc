#include "v2doc/output_transaction.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace v2doc {
namespace {

constexpr std::string_view report_marker = "v2doc-report-v1\n";

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

std::filesystem::path unique_sibling(
    const std::filesystem::path &target, const std::string_view role) {
    static std::atomic<unsigned long> next{0};
    const auto parent =
        target.has_parent_path() ? target.parent_path()
                                 : std::filesystem::path{"."};
    const auto basename = target.filename().string();
    for (unsigned attempt = 0; attempt < 1000U; ++attempt) {
        const auto candidate =
            parent /
            ("." + basename + ".v2doc-" + std::string{role} + "-" +
             std::to_string(::getpid()) + "-" +
             std::to_string(next.fetch_add(1)));
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("unable to allocate a unique output path");
}

void validate_staging(const std::filesystem::path &staging) {
    if (!is_v2doc_report(staging)) {
        throw std::runtime_error("staged report marker is missing");
    }
    constexpr std::string_view required[] = {
        "index.html",
        "transcript.txt",
        "transcript.json",
    };
    for (const auto filename : required) {
        const auto path = staging / filename;
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error(
                "staged report is incomplete: " + std::string{filename});
        }
    }
    if (!std::filesystem::is_directory(staging / "thumbnails")) {
        throw std::runtime_error(
            "staged report is incomplete: thumbnails");
    }
}

}  // namespace

bool is_v2doc_report(const std::filesystem::path &directory) {
    return std::filesystem::is_directory(directory) &&
           read_file(directory / ".v2doc-report") == report_marker;
}

void validate_output_target(
    const std::filesystem::path &target, const bool force) {
    if (target.empty()) {
        throw std::invalid_argument("output directory cannot be empty");
    }
    if (!std::filesystem::exists(target)) {
        return;
    }
    if (!force) {
        throw std::runtime_error(
            "output directory already exists; use --force to replace a "
            "v2doc report");
    }
    const bool empty_directory =
        std::filesystem::is_directory(target) &&
        std::filesystem::is_empty(target);
    if (!empty_directory && !is_v2doc_report(target)) {
        throw std::runtime_error(
            "refusing to replace a directory that is not a v2doc report");
    }
}

OutputTransaction::OutputTransaction(
    std::filesystem::path target, const bool force)
    : target_(std::move(target)) {
    validate_output_target(target_, force);

    const auto parent =
        target_.has_parent_path() ? target_.parent_path()
                                  : std::filesystem::path{"."};
    std::filesystem::create_directories(parent);
    staging_ = unique_sibling(target_, "staging");
    if (!std::filesystem::create_directory(staging_)) {
        throw std::runtime_error("unable to create report staging directory");
    }
}

OutputTransaction::~OutputTransaction() {
    if (!committed_ && !staging_.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(staging_, ignored);
    }
}

const std::filesystem::path &OutputTransaction::staging_path() const {
    return staging_;
}

void OutputTransaction::commit() {
    if (committed_) {
        throw std::logic_error("output transaction is already committed");
    }
    validate_staging(staging_);

    std::filesystem::path backup;
    if (std::filesystem::exists(target_)) {
        backup = unique_sibling(target_, "backup");
        std::filesystem::rename(target_, backup);
    }

    std::error_code install_error;
    std::filesystem::rename(staging_, target_, install_error);
    if (install_error) {
        if (!backup.empty()) {
            std::error_code restore_error;
            std::filesystem::rename(backup, target_, restore_error);
        }
        throw std::runtime_error(
            "unable to install completed report: " +
            install_error.message());
    }

    committed_ = true;
    if (!backup.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    }
}

}  // namespace v2doc
