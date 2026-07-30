#include "v2doc/output_transaction.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned> next{0};
        path_ = std::filesystem::temp_directory_path() /
                ("v2doc-output-test-" + std::to_string(::getpid()) + "-" +
                 std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() { std::filesystem::remove_all(path_); }

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(
    const std::filesystem::path &path, const std::string &contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << contents;
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

void write_complete_report(const std::filesystem::path &directory) {
    write_file(directory / ".v2doc-report", "v2doc-report-v1\n");
    write_file(directory / "index.html", "<html></html>");
    write_file(directory / "transcript.txt", "text");
    write_file(directory / "transcript.json", "{}");
    std::filesystem::create_directories(directory / "thumbnails");
}

}  // namespace

TEST_CASE("output transaction commits a complete new report") {
    TempDirectory temp;
    const auto target = temp.path() / "report";

    v2doc::OutputTransaction transaction(target, false);
    const auto staging = transaction.staging_path();
    write_complete_report(staging);
    transaction.commit();

    CHECK(std::filesystem::is_directory(target));
    CHECK(read_file(target / ".v2doc-report") == "v2doc-report-v1\n");
    CHECK_FALSE(std::filesystem::exists(staging));
}

TEST_CASE("existing output requires force") {
    TempDirectory temp;
    const auto target = temp.path() / "report";
    write_complete_report(target);

    CHECK_THROWS_WITH(
        v2doc::OutputTransaction(target, false),
        "output directory already exists; use --force to replace a v2doc report");
}

TEST_CASE("force refuses an unrelated nonempty directory") {
    TempDirectory temp;
    const auto target = temp.path() / "personal";
    write_file(target / "personal.txt", "keep");

    CHECK_THROWS_WITH(
        v2doc::OutputTransaction(target, true),
        "refusing to replace a directory that is not a v2doc report");
    CHECK(read_file(target / "personal.txt") == "keep");
}

TEST_CASE("force replaces only a marked report") {
    TempDirectory temp;
    const auto target = temp.path() / "report";
    write_complete_report(target);
    write_file(target / "old.txt", "old");

    v2doc::OutputTransaction transaction(target, true);
    write_complete_report(transaction.staging_path());
    write_file(transaction.staging_path() / "new.txt", "new");
    transaction.commit();

    CHECK_FALSE(std::filesystem::exists(target / "old.txt"));
    CHECK(read_file(target / "new.txt") == "new");
}

TEST_CASE("uncommitted staging is removed without touching the target") {
    TempDirectory temp;
    const auto target = temp.path() / "report";
    std::filesystem::path staging;
    {
        v2doc::OutputTransaction transaction(target, false);
        staging = transaction.staging_path();
        write_file(staging / "partial.txt", "partial");
    }

    CHECK_FALSE(std::filesystem::exists(staging));
    CHECK_FALSE(std::filesystem::exists(target));
}

TEST_CASE("incomplete staging cannot replace an existing report") {
    TempDirectory temp;
    const auto target = temp.path() / "report";
    write_complete_report(target);
    write_file(target / "old.txt", "preserved");

    v2doc::OutputTransaction transaction(target, true);
    write_file(
        transaction.staging_path() / ".v2doc-report",
        "v2doc-report-v1\n");

    CHECK_THROWS_WITH(
        transaction.commit(),
        "staged report is incomplete: index.html");
    CHECK(read_file(target / "old.txt") == "preserved");
}

TEST_CASE("report marker must match exactly") {
    TempDirectory temp;
    const auto target = temp.path() / "report";
    write_file(target / ".v2doc-report", "another-format\n");

    CHECK_FALSE(v2doc::is_v2doc_report(target));
    CHECK_THROWS_WITH(
        v2doc::OutputTransaction(target, true),
        "refusing to replace a directory that is not a v2doc report");
}

