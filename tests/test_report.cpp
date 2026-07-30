#include "v2doc/report.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <nlohmann/json.hpp>

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned> next{0};
        path_ = std::filesystem::temp_directory_path() /
                ("v2doc-report-test-" + std::to_string(::getpid()) + "-" +
                 std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() { std::filesystem::remove_all(path_); }

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

}  // namespace

TEST_CASE("timestamp includes hours minutes seconds and milliseconds") {
    CHECK(v2doc::format_timestamp(0) == "00:00:00.000");
    CHECK(v2doc::format_timestamp(3'723'004) == "01:02:03.004");
    CHECK_THROWS_WITH(
        v2doc::format_timestamp(-1),
        "timestamp cannot be negative");
}

TEST_CASE("report writes escaped offline HTML structured JSON and plain text") {
    TempDirectory temp;
    std::filesystem::create_directories(temp.path() / "thumbnails");
    std::ofstream(temp.path() / "thumbnails/segment-0001.jpg") << "jpeg";
    const std::vector blocks{
        v2doc::TranscriptBlock{
            0,
            1000,
            "speaker-1",
            "<script>alert(\"x\")</script> & café 日本語",
            "thumbnails/segment-0001.jpg",
        },
        v2doc::TranscriptBlock{
            1100,
            2200,
            "speaker-unknown",
            "No image",
            std::nullopt,
        },
    };
    const std::vector<std::string> warnings{
        "Diarization unavailable <retry>",
    };

    v2doc::write_report(
        temp.path(), "movie & test.mp4", "ja", blocks, warnings);

    const auto html = read_file(temp.path() / "index.html");
    CHECK(html.find("<script>alert") == std::string::npos);
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    CHECK(html.find("movie &amp; test.mp4") != std::string::npos);
    CHECK(
        html.find("src=\"thumbnails/segment-0001.jpg\"") !=
        std::string::npos);
    CHECK(html.find("Thumbnail unavailable") != std::string::npos);
    CHECK(html.find("<script src=") == std::string::npos);
    CHECK(html.find("href=\"http") == std::string::npos);
    CHECK(html.find("src=\"http") == std::string::npos);

    const auto json =
        nlohmann::json::parse(read_file(temp.path() / "transcript.json"));
    CHECK(json.at("schema") == "v2doc-transcript-v1");
    CHECK(json.at("input") == "movie & test.mp4");
    CHECK(json.at("language") == "ja");
    REQUIRE(json.at("blocks").size() == 2);
    CHECK(json.at("blocks")[0].at("start_ms") == 0);
    CHECK(json.at("blocks")[0].at("end_ms") == 1000);
    CHECK(json.at("blocks")[0].at("speaker") == "speaker-1");
    CHECK(
        json.at("blocks")[0].at("thumbnail") ==
        "thumbnails/segment-0001.jpg");
    CHECK(json.at("blocks")[1].at("thumbnail").is_null());
    CHECK(json.at("warnings")[0] == "Diarization unavailable <retry>");

    const auto text = read_file(temp.path() / "transcript.txt");
    CHECK(
        text.find(
            "[00:00:00.000 --> 00:00:01.000] speaker-1: "
            "<script>alert(\"x\")</script> & café 日本語") !=
        std::string::npos);
    CHECK(
        read_file(temp.path() / ".v2doc-report") ==
        "v2doc-report-v1\n");
}

TEST_CASE("report rejects thumbnail paths outside its output directory") {
    TempDirectory temp;
    const std::vector absolute{
        v2doc::TranscriptBlock{
            0, 1, "speaker-1", "text", "/tmp/outside.jpg"},
    };
    CHECK_THROWS_WITH(
        v2doc::write_report(temp.path(), "movie.mp4", "en", absolute, {}),
        "thumbnail path must stay inside thumbnails/");

    const std::vector traversal{
        v2doc::TranscriptBlock{
            0, 1, "speaker-1", "text", "thumbnails/../outside.jpg"},
    };
    CHECK_THROWS_WITH(
        v2doc::write_report(temp.path(), "movie.mp4", "en", traversal, {}),
        "thumbnail path must stay inside thumbnails/");
}

