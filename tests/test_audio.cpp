#include "v2doc/audio.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

class TempDirectory {
public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("v2doc-audio-test-" + std::to_string(::getpid()))) {
        std::filesystem::remove_all(path_);
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

std::filesystem::path write_wave(
    const std::filesystem::path &path,
    const std::vector<std::int16_t> &samples,
    const std::uint16_t channels = 1,
    const std::uint32_t sample_rate = 16000,
    const std::uint16_t format = 1) {
    std::ofstream stream(path, std::ios::binary);
    const auto data_bytes =
        static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    stream.write("RIFF", 4);
    write_u32(stream, 36U + data_bytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    write_u32(stream, 16);
    write_u16(stream, format);
    write_u16(stream, channels);
    write_u32(stream, sample_rate);
    write_u32(stream, sample_rate * channels * 2U);
    write_u16(stream, static_cast<std::uint16_t>(channels * 2U));
    write_u16(stream, 16);
    stream.write("data", 4);
    write_u32(stream, data_bytes);
    for (const auto sample : samples) {
        write_u16(stream, static_cast<std::uint16_t>(sample));
    }
    return path;
}

class RecordingRunner final : public v2doc::ProcessRunner {
public:
    mutable std::vector<std::vector<std::string>> calls;
    int exit_code{};
    std::string stderr_text;
    bool create_output{true};

    v2doc::ProcessResult run(
        const std::vector<std::string> &args) const override {
        calls.push_back(args);
        if (exit_code == 0 && create_output) {
            write_wave(args.back(), {0, 1, -1});
        }
        return {exit_code, stderr_text};
    }
};

}  // namespace

TEST_CASE("PCM16 mono samples are normalized to floats") {
    TempDirectory temp;
    const auto path =
        write_wave(temp.path() / "audio.wav", {-32768, 0, 32767});

    const auto audio = v2doc::read_pcm16_wave(path);

    CHECK(audio.sample_rate == 16000);
    REQUIRE(audio.samples.size() == 3);
    CHECK(audio.samples[0] == Catch::Approx(-1.0F));
    CHECK(audio.samples[1] == Catch::Approx(0.0F));
    CHECK(audio.samples[2] == Catch::Approx(32767.0F / 32768.0F));
}

TEST_CASE("WAV reader rejects unsupported layouts") {
    TempDirectory temp;

    CHECK_THROWS_WITH(
        v2doc::read_pcm16_wave(
            write_wave(temp.path() / "stereo.wav", {0, 0}, 2)),
        "WAV audio must be mono");
    CHECK_THROWS_WITH(
        v2doc::read_pcm16_wave(
            write_wave(temp.path() / "rate.wav", {0}, 1, 44100)),
        "WAV sample rate must be 16000 Hz");
    CHECK_THROWS_WITH(
        v2doc::read_pcm16_wave(
            write_wave(temp.path() / "float.wav", {0}, 1, 16000, 3)),
        "WAV audio must use PCM encoding");
}

TEST_CASE("WAV reader rejects malformed and truncated input") {
    TempDirectory temp;
    const auto invalid = temp.path() / "invalid.wav";
    {
        std::ofstream stream(invalid, std::ios::binary);
        stream.write("NOPE", 4);
    }
    CHECK_THROWS_WITH(
        v2doc::read_pcm16_wave(invalid), "invalid WAV RIFF header");

    const auto truncated =
        write_wave(temp.path() / "truncated.wav", {1, 2, 3});
    std::filesystem::resize_file(
        truncated, std::filesystem::file_size(truncated) - 1);
    CHECK_THROWS_WITH(
        v2doc::read_pcm16_wave(truncated), "truncated WAV sample data");
}

TEST_CASE("audio extraction uses a fixed FFmpeg argument vector") {
    TempDirectory temp;
    RecordingRunner runner;

    const auto output = v2doc::extract_audio(
        runner, "input name;safe.mp4", temp.path());

    CHECK(output == temp.path() / "audio.wav");
    REQUIRE(runner.calls.size() == 1);
    CHECK(runner.calls.front() == std::vector<std::string>{
        "ffmpeg",
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        "input name;safe.mp4",
        "-vn",
        "-ac",
        "1",
        "-ar",
        "16000",
        "-c:a",
        "pcm_s16le",
        output.string(),
    });
}

TEST_CASE("audio extraction reports FFmpeg failures") {
    TempDirectory temp;
    RecordingRunner runner;
    runner.exit_code = 9;
    runner.stderr_text = "no audio stream";

    CHECK_THROWS_WITH(
        v2doc::extract_audio(runner, "silent.mp4", temp.path()),
        "FFmpeg audio extraction failed: no audio stream");
}

TEST_CASE("audio extraction rejects a missing output file") {
    TempDirectory temp;
    RecordingRunner runner;
    runner.create_output = false;

    CHECK_THROWS_WITH(
        v2doc::extract_audio(runner, "movie.mp4", temp.path()),
        "FFmpeg did not produce extracted audio");
}
