#include "v2doc/thumbnailer.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned> next{0};
        path_ = std::filesystem::temp_directory_path() /
                ("v2doc-thumbnail-test-" + std::to_string(::getpid()) + "-" +
                 std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() { std::filesystem::remove_all(path_); }

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

class RecordingRunner final : public v2doc::ProcessRunner {
public:
    mutable std::vector<std::vector<std::string>> calls;
    mutable std::size_t call_count{};
    std::vector<int> exit_codes;
    bool write_outputs{true};

    v2doc::ProcessResult run(
        const std::vector<std::string> &args) const override {
        calls.push_back(args);
        const auto index = call_count++;
        const auto exit_code =
            index < exit_codes.size() ? exit_codes[index] : 0;
        if (exit_code == 0 && write_outputs) {
            std::ofstream(args.back(), std::ios::binary) << "jpeg";
        }
        return {exit_code, exit_code == 0 ? "" : "decode failed"};
    }
};

}  // namespace

TEST_CASE("thumbnail extraction seeks to each block midpoint") {
    TempDirectory temp;
    RecordingRunner runner;
    v2doc::FfmpegThumbnailer thumbnailer(runner);
    std::vector blocks{
        v2doc::TranscriptBlock{
            1000, 3000, "speaker-1", "text", std::nullopt},
    };

    const auto warnings =
        thumbnailer.create("input name;safe.mp4", temp.path(), blocks, 320);

    CHECK(warnings.empty());
    REQUIRE(runner.calls.size() == 1);
    CHECK(runner.calls[0] == std::vector<std::string>{
        "ffmpeg",
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-ss",
        "2.000",
        "-i",
        "input name;safe.mp4",
        "-frames:v",
        "1",
        "-vf",
        "scale=320:-2",
        "-q:v",
        "3",
        (temp.path() / "thumbnails/segment-0001.jpg").string(),
    });
    REQUIRE(blocks[0].thumbnail.has_value());
    CHECK(*blocks[0].thumbnail == "thumbnails/segment-0001.jpg");
}

TEST_CASE("thumbnail names are sequential and zero padded") {
    TempDirectory temp;
    RecordingRunner runner;
    v2doc::FfmpegThumbnailer thumbnailer(runner);
    std::vector blocks{
        v2doc::TranscriptBlock{0, 100, "speaker-1", "one", std::nullopt},
        v2doc::TranscriptBlock{100, 200, "speaker-1", "two", std::nullopt},
    };

    const auto warnings =
        thumbnailer.create("movie.mp4", temp.path(), blocks, 480);

    CHECK(warnings.empty());
    CHECK(*blocks[0].thumbnail == "thumbnails/segment-0001.jpg");
    CHECK(*blocks[1].thumbnail == "thumbnails/segment-0002.jpg");
    CHECK(
        runner.calls[1][runner.calls[1].size() - 4] ==
        "scale=480:-2");
}

TEST_CASE("one failed thumbnail does not prevent later frames") {
    TempDirectory temp;
    RecordingRunner runner;
    runner.exit_codes = {4, 0};
    v2doc::FfmpegThumbnailer thumbnailer(runner);
    std::vector blocks{
        v2doc::TranscriptBlock{0, 100, "speaker-1", "one", std::nullopt},
        v2doc::TranscriptBlock{100, 200, "speaker-1", "two", std::nullopt},
    };

    const auto warnings =
        thumbnailer.create("movie.mp4", temp.path(), blocks, 320);

    REQUIRE(warnings.size() == 1);
    CHECK(
        warnings[0].find("00:00:00.000") != std::string::npos);
    CHECK_FALSE(blocks[0].thumbnail.has_value());
    CHECK(blocks[1].thumbnail.has_value());
}

TEST_CASE("successful FFmpeg without a file is treated as a frame failure") {
    TempDirectory temp;
    RecordingRunner runner;
    runner.write_outputs = false;
    v2doc::FfmpegThumbnailer thumbnailer(runner);
    std::vector blocks{
        v2doc::TranscriptBlock{0, 100, "speaker-1", "one", std::nullopt},
    };

    const auto warnings =
        thumbnailer.create("movie.mp4", temp.path(), blocks, 320);

    REQUIRE(warnings.size() == 1);
    CHECK_FALSE(blocks[0].thumbnail.has_value());
}

TEST_CASE("thumbnail width must be positive") {
    TempDirectory temp;
    RecordingRunner runner;
    v2doc::FfmpegThumbnailer thumbnailer(runner);
    std::vector<v2doc::TranscriptBlock> blocks;

    CHECK_THROWS_WITH(
        thumbnailer.create("movie.mp4", temp.path(), blocks, 0),
        "thumbnail width must be greater than zero");
}
