#include "v2doc/cli.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

using namespace std::string_view_literals;
using v2doc::default_output_path;
using v2doc::parse_cli;

TEST_CASE("default output uses the complete input stem") {
    CHECK(default_output_path("/media/interview.final.mp4") ==
          std::filesystem::path{"interview.final-transcript"});
}

TEST_CASE("default output handles hidden and extensionless input names") {
    CHECK(default_output_path(".meeting") ==
          std::filesystem::path{".meeting-transcript"});
    CHECK(default_output_path("recording") ==
          std::filesystem::path{"recording-transcript"});
}

TEST_CASE("default output preserves Unicode filenames") {
    CHECK(default_output_path("/media/会議.mp4") ==
          std::filesystem::path{"会議-transcript"});
}

TEST_CASE("CLI defaults to multilingual detection and standard thumbnails") {
    const std::array args{"v2doc"sv, "movie.mp4"sv};
    const auto result = parse_cli(args);

    REQUIRE(result.options.has_value());
    CHECK(result.options->input == "movie.mp4");
    CHECK(result.options->output == "movie-transcript");
    CHECK(result.options->language == std::nullopt);
    CHECK(result.options->thumbnail_width == 320);
    CHECK(result.options->diarization);
    CHECK_FALSE(result.options->force);
}

TEST_CASE("CLI accepts all processing overrides") {
    const std::array args{
        "v2doc"sv,
        "movie.mp4"sv,
        "--output"sv,
        "custom-report"sv,
        "--model"sv,
        "small.bin"sv,
        "--diarization-models"sv,
        "diarization"sv,
        "--language"sv,
        "fr"sv,
        "--speakers"sv,
        "3"sv,
        "--thumbnail-width"sv,
        "480"sv,
        "--no-diarization"sv,
        "--force"sv,
    };
    const auto result = parse_cli(args);

    REQUIRE(result.options.has_value());
    CHECK(result.options->output == "custom-report");
    CHECK(result.options->whisper_model == "small.bin");
    CHECK(result.options->diarization_models == "diarization");
    CHECK(result.options->language == std::optional<std::string>{"fr"});
    CHECK(result.options->speaker_count == std::optional<int>{3});
    CHECK(result.options->thumbnail_width == 480);
    CHECK_FALSE(result.options->diarization);
    CHECK(result.options->force);
}

TEST_CASE("CLI reports help without requiring an input") {
    const std::array args{"v2doc"sv, "--help"sv};
    const auto result = parse_cli(args);

    CHECK(result.show_help);
    CHECK_FALSE(result.options.has_value());
    CHECK(result.error.empty());
}

TEST_CASE("CLI rejects invalid numeric options") {
    const std::array zero_speakers{
        "v2doc"sv, "movie.mp4"sv, "--speakers"sv, "0"sv};
    CHECK(parse_cli(zero_speakers).error ==
          "--speakers must be greater than zero");

    const std::array zero_width{
        "v2doc"sv, "movie.mp4"sv, "--thumbnail-width"sv, "0"sv};
    CHECK(parse_cli(zero_width).error ==
          "--thumbnail-width must be greater than zero");

    const std::array invalid{
        "v2doc"sv, "movie.mp4"sv, "--speakers"sv, "many"sv};
    CHECK(parse_cli(invalid).error ==
          "--speakers requires a positive integer");
}

TEST_CASE("CLI rejects missing option values") {
    const std::array missing_output{
        "v2doc"sv, "movie.mp4"sv, "--output"sv};
    CHECK(parse_cli(missing_output).error ==
          "--output requires a directory");

    const std::array missing_language{
        "v2doc"sv, "movie.mp4"sv, "--language"sv};
    CHECK(parse_cli(missing_language).error ==
          "--language requires a language code");
}

TEST_CASE("CLI rejects ambiguous inputs and unknown options") {
    const std::array multiple{
        "v2doc"sv, "one.mp4"sv, "two.mp4"sv};
    CHECK(parse_cli(multiple).error == "only one input video is supported");

    const std::array unknown{
        "v2doc"sv, "movie.mp4"sv, "--cloud"sv};
    CHECK(parse_cli(unknown).error == "unknown option: --cloud");

    const std::array none{"v2doc"sv};
    CHECK(parse_cli(none).error == "an input video is required");
}
