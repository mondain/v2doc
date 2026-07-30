#include "v2doc/process.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using v2doc::PosixProcessRunner;

TEST_CASE("process runner passes shell metacharacters as literal arguments") {
    const auto marker =
        std::filesystem::temp_directory_path() / "v2doc-must-not-exist";
    std::filesystem::remove(marker);

    PosixProcessRunner runner;
    const auto result = runner.run(
        {"/usr/bin/printf", "%s", "name;$(touch " + marker.string() + ")"});

    CHECK(result.exit_code == 0);
    CHECK_FALSE(std::filesystem::exists(marker));
}

TEST_CASE("process runner captures stderr and preserves the exit code") {
    PosixProcessRunner runner;
    const auto result = runner.run(
        {"/bin/sh", "-c", "printf 'specific failure' >&2; exit 7"});

    CHECK(result.exit_code == 7);
    CHECK(result.stderr_text == "specific failure");
}

TEST_CASE("process runner reports a missing executable") {
    PosixProcessRunner runner;
    const auto result = runner.run({"/definitely/missing/v2doc-command"});

    CHECK(result.exit_code == 127);
    CHECK(result.stderr_text.find("execvp") != std::string::npos);
}

TEST_CASE("process runner rejects an empty argument vector") {
    PosixProcessRunner runner;
    CHECK_THROWS_AS(runner.run({}), std::invalid_argument);
}

