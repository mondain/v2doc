#include "v2doc/pipeline.hpp"

#include "v2doc/diarizer.hpp"
#include "v2doc/process.hpp"
#include "v2doc/thumbnailer.hpp"
#include "v2doc/transcriber.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long> next{0};
        path_ = std::filesystem::temp_directory_path() /
                ("v2doc-pipeline-test-" + std::to_string(::getpid()) + "-" +
                 std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() { std::filesystem::remove_all(path_); }

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_u16(std::ofstream &stream, const std::uint16_t value) {
    const std::array bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ofstream &stream, const std::uint32_t value) {
    const std::array bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU),
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_wave(
    const std::filesystem::path &path,
    const std::size_t sample_count = 16000U) {
    std::ofstream stream(path, std::ios::binary);
    const auto data_bytes =
        static_cast<std::uint32_t>(sample_count * sizeof(std::int16_t));
    stream.write("RIFF", 4);
    write_u32(stream, 36U + data_bytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    write_u32(stream, 16U);
    write_u16(stream, 1U);
    write_u16(stream, 1U);
    write_u32(stream, 16000U);
    write_u32(stream, 32000U);
    write_u16(stream, 2U);
    write_u16(stream, 16U);
    stream.write("data", 4);
    write_u32(stream, data_bytes);
    for (std::size_t index = 0; index < sample_count; ++index) {
        write_u16(stream, 0U);
    }
}

void write_file(
    const std::filesystem::path &path,
    const std::string &content = "model") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << content;
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

v2doc::AppOptions valid_options(
    const std::filesystem::path &root,
    const std::filesystem::path &input,
    const std::filesystem::path &output) {
    if (!std::filesystem::exists(input)) {
        write_file(input, "video");
    }
    const auto models = root / "models";
    const auto diarization = models / "diarization";
    write_file(models / "ggml-small.bin");
    write_file(
        diarization /
        "sherpa-onnx-pyannote-segmentation-3-0" / "model.onnx");
    write_file(
        diarization /
        "3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx");

    v2doc::AppOptions options;
    options.input = input;
    options.output = output;
    options.whisper_model = models / "ggml-small.bin";
    options.diarization_models = diarization;
    return options;
}

class FakeRunner final : public v2doc::ProcessRunner {
public:
    bool available{true};
    bool extraction_succeeds{true};
    bool empty_audio{false};
    mutable std::vector<std::vector<std::string>> calls;

    v2doc::ProcessResult run(
        const std::vector<std::string> &args) const override {
        calls.push_back(args);
        if (!args.empty() && args.back() == "-") {
            return available ? v2doc::ProcessResult{}
                             : v2doc::ProcessResult{127, "not found"};
        }
        if (!extraction_succeeds) {
            return {1, "no audio stream"};
        }
        write_wave(args.back(), empty_audio ? 0U : 16000U);
        return {};
    }
};

class FakeTranscriber final : public v2doc::Transcriber {
public:
    v2doc::TranscriptResult result{
        "en",
        {{0, 400, "Hello"}, {450, 900, "world."}},
    };
    bool fail{false};
    mutable int calls{};

    v2doc::TranscriptResult transcribe(
        const v2doc::AudioBuffer &,
        const v2doc::AppOptions &) const override {
        ++calls;
        if (fail) {
            throw std::runtime_error("transcription failed");
        }
        return result;
    }
};

class FakeDiarizer final : public v2doc::Diarizer {
public:
    std::vector<v2doc::SpeakerInterval> result{{0, 1000, 9}};
    bool fail{false};
    mutable int calls{};

    std::vector<v2doc::SpeakerInterval> diarize(
        const v2doc::AudioBuffer &,
        const v2doc::AppOptions &) const override {
        ++calls;
        if (fail) {
            throw std::runtime_error("clustering failed");
        }
        return result;
    }
};

class FakeThumbnailer final : public v2doc::Thumbnailer {
public:
    bool fail{false};

    std::vector<std::string> create(
        const std::filesystem::path &,
        const std::filesystem::path &staging,
        std::vector<v2doc::TranscriptBlock> &blocks,
        int) const override {
        std::filesystem::create_directories(staging / "thumbnails");
        if (fail) {
            return {"Thumbnail unavailable at 00:00:00.000"};
        }
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            const auto relative = std::filesystem::path{"thumbnails"} /
                                  ("frame-" + std::to_string(index) + ".jpg");
            write_file(staging / relative, "jpeg");
            blocks[index].thumbnail = relative;
        }
        return {};
    }
};

}  // namespace

TEST_CASE("pipeline generates a complete marked report") {
    TempDirectory temp;
    FakeRunner runner;
    FakeTranscriber transcriber;
    FakeDiarizer diarizer;
    FakeThumbnailer thumbnails;
    v2doc::Pipeline pipeline(runner, transcriber, diarizer, thumbnails);
    const auto output = temp.path() / "report";
    const auto options =
        valid_options(temp.path(), temp.path() / "movie.mp4", output);

    const auto result = pipeline.run(options);

    CHECK(result.output == output);
    CHECK(result.warnings.empty());
    CHECK(read_file(output / ".v2doc-report") == "v2doc-report-v1\n");
    CHECK(std::filesystem::is_regular_file(output / "index.html"));
    CHECK(read_file(output / "transcript.txt").find("speaker-1") !=
          std::string::npos);
}

TEST_CASE("pipeline validates required local dependencies") {
    TempDirectory temp;
    FakeRunner runner;
    FakeTranscriber transcriber;
    FakeDiarizer diarizer;
    FakeThumbnailer thumbnails;
    v2doc::Pipeline pipeline(runner, transcriber, diarizer, thumbnails);

    auto options = valid_options(
        temp.path(), temp.path() / "movie.mp4", temp.path() / "report");
    std::filesystem::remove(options.input);
    CHECK_THROWS_WITH(
        pipeline.run(options), "input video is not a regular file");

    options = valid_options(
        temp.path(), temp.path() / "movie.mp4", temp.path() / "report");
    runner.available = false;
    CHECK_THROWS_WITH(
        pipeline.run(options), "FFmpeg is unavailable: not found");
    runner.available = true;

    std::filesystem::remove(options.whisper_model);
    CHECK_THROWS_WITH(
        pipeline.run(options), "Whisper model is not a regular file");

    write_file(options.whisper_model);
    std::filesystem::remove_all(options.diarization_models);
    CHECK_THROWS_WITH(
        pipeline.run(options), "diarization model files are missing");
}

TEST_CASE("pipeline reports media without an audio stream") {
    TempDirectory temp;
    FakeRunner runner;
    runner.extraction_succeeds = false;
    FakeTranscriber transcriber;
    FakeDiarizer diarizer;
    FakeThumbnailer thumbnails;
    v2doc::Pipeline pipeline(runner, transcriber, diarizer, thumbnails);
    const auto options = valid_options(
        temp.path(), temp.path() / "silent.mp4", temp.path() / "report");

    CHECK_THROWS_WITH(
        pipeline.run(options),
        "FFmpeg audio extraction failed: no audio stream");
    CHECK_FALSE(std::filesystem::exists(options.output));
}

TEST_CASE("diarization is optional and its runtime failure is recoverable") {
    TempDirectory temp;
    FakeRunner runner;
    FakeTranscriber transcriber;
    FakeDiarizer diarizer;
    FakeThumbnailer thumbnails;
    v2doc::Pipeline pipeline(runner, transcriber, diarizer, thumbnails);

    auto options = valid_options(
        temp.path(), temp.path() / "movie.mp4", temp.path() / "without");
    options.diarization = false;
    std::filesystem::remove_all(options.diarization_models);
    const auto without = pipeline.run(options);
    CHECK(diarizer.calls == 0);
    CHECK(read_file(without.output / "transcript.txt").find(
              "speaker-unknown") != std::string::npos);

    options = valid_options(
        temp.path(), temp.path() / "movie.mp4", temp.path() / "fallback");
    diarizer.fail = true;
    const auto fallback = pipeline.run(options);
    REQUIRE(fallback.warnings.size() == 1U);
    CHECK(fallback.warnings[0] ==
          "Speaker diarization unavailable: clustering failed");
}

TEST_CASE("thumbnail failures are retained as report warnings") {
    TempDirectory temp;
    FakeRunner runner;
    FakeTranscriber transcriber;
    FakeDiarizer diarizer;
    FakeThumbnailer thumbnails;
    thumbnails.fail = true;
    v2doc::Pipeline pipeline(runner, transcriber, diarizer, thumbnails);
    const auto options = valid_options(
        temp.path(), temp.path() / "movie.mp4", temp.path() / "report");

    const auto result = pipeline.run(options);

    REQUIRE(result.warnings.size() == 1U);
    CHECK(read_file(result.output / "index.html").find(
              "Thumbnail unavailable") != std::string::npos);
}

TEST_CASE("fatal transcription failure does not install output") {
    TempDirectory temp;
    FakeRunner runner;
    FakeTranscriber transcriber;
    transcriber.fail = true;
    FakeDiarizer diarizer;
    FakeThumbnailer thumbnails;
    v2doc::Pipeline pipeline(runner, transcriber, diarizer, thumbnails);
    const auto options = valid_options(
        temp.path(), temp.path() / "movie.mp4", temp.path() / "report");

    CHECK_THROWS_WITH(pipeline.run(options), "transcription failed");
    CHECK_FALSE(std::filesystem::exists(options.output));
}

TEST_CASE("real FFmpeg extraction and thumbnails produce a portable report") {
    TempDirectory temp;
    v2doc::PosixProcessRunner runner;
    const auto video = temp.path() / "fixture.mp4";
    const auto generated = runner.run({
        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "color=c=blue:s=320x180:d=2",
        "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
        "-shortest", "-c:v", "mpeg4", "-c:a", "aac", video.string(),
    });
    REQUIRE(generated.exit_code == 0);

    FakeTranscriber transcriber;
    FakeDiarizer diarizer;
    v2doc::FfmpegThumbnailer thumbnails(runner);
    v2doc::Pipeline pipeline(runner, transcriber, diarizer, thumbnails);
    const auto options =
        valid_options(temp.path(), video, temp.path() / "report");

    const auto result = pipeline.run(options);

    const auto thumbnail =
        result.output / "thumbnails" / "segment-0001.jpg";
    CHECK(std::filesystem::is_regular_file(thumbnail));
    CHECK(std::filesystem::file_size(thumbnail) > 0U);
    CHECK(std::filesystem::is_regular_file(result.output / "transcript.json"));
}
