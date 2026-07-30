#include "v2doc/audio.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace v2doc {
namespace {

std::uint16_t read_u16(
    const std::vector<std::uint8_t> &bytes, const std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
        throw std::runtime_error("truncated WAV chunk");
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

std::uint32_t read_u32(
    const std::vector<std::uint8_t> &bytes, const std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        throw std::runtime_error("truncated WAV chunk");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

bool matches(
    const std::vector<std::uint8_t> &bytes,
    const std::size_t offset,
    const char (&value)[5]) {
    return offset <= bytes.size() && bytes.size() - offset >= 4U &&
           std::equal(value, value + 4, bytes.begin() +
                                            static_cast<std::ptrdiff_t>(offset));
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to open WAV file: " + path.string());
    }
    return {
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

}  // namespace

AudioBuffer read_pcm16_wave(const std::filesystem::path &path) {
    const auto bytes = read_bytes(path);
    if (!matches(bytes, 0, "RIFF") || !matches(bytes, 8, "WAVE")) {
        throw std::runtime_error("invalid WAV RIFF header");
    }

    bool found_format = false;
    bool found_data = false;
    std::uint16_t encoding{};
    std::uint16_t channels{};
    std::uint32_t sample_rate{};
    std::uint16_t bits_per_sample{};
    std::size_t data_offset{};
    std::size_t data_size{};

    std::size_t offset = 12;
    while (offset <= bytes.size() && bytes.size() - offset >= 8U) {
        const auto chunk_size =
            static_cast<std::size_t>(read_u32(bytes, offset + 4U));
        const auto payload = offset + 8U;
        if (payload > bytes.size() || chunk_size > bytes.size() - payload) {
            if (matches(bytes, offset, "data")) {
                throw std::runtime_error("truncated WAV sample data");
            }
            throw std::runtime_error("truncated WAV chunk");
        }

        if (matches(bytes, offset, "fmt ")) {
            if (chunk_size < 16U) {
                throw std::runtime_error("invalid WAV format chunk");
            }
            encoding = read_u16(bytes, payload);
            channels = read_u16(bytes, payload + 2U);
            sample_rate = read_u32(bytes, payload + 4U);
            bits_per_sample = read_u16(bytes, payload + 14U);
            found_format = true;
        } else if (matches(bytes, offset, "data")) {
            data_offset = payload;
            data_size = chunk_size;
            found_data = true;
        }

        const auto padding = chunk_size % 2U;
        if (chunk_size > bytes.size() - payload - padding) {
            break;
        }
        offset = payload + chunk_size + padding;
    }

    if (!found_format) {
        throw std::runtime_error("WAV format chunk is missing");
    }
    if (!found_data) {
        throw std::runtime_error("WAV data chunk is missing");
    }
    if (encoding != 1U) {
        throw std::runtime_error("WAV audio must use PCM encoding");
    }
    if (channels != 1U) {
        throw std::runtime_error("WAV audio must be mono");
    }
    if (sample_rate != 16000U) {
        throw std::runtime_error("WAV sample rate must be 16000 Hz");
    }
    if (bits_per_sample != 16U) {
        throw std::runtime_error("WAV audio must be 16-bit");
    }
    if (data_size % 2U != 0U) {
        throw std::runtime_error("truncated WAV sample data");
    }

    AudioBuffer result;
    result.sample_rate = static_cast<int>(sample_rate);
    result.samples.reserve(data_size / 2U);
    for (std::size_t index = 0; index < data_size; index += 2U) {
        const auto raw = read_u16(bytes, data_offset + index);
        const auto sample = std::bit_cast<std::int16_t>(raw);
        result.samples.push_back(static_cast<float>(sample) / 32768.0F);
    }
    return result;
}

std::filesystem::path extract_audio(
    const ProcessRunner &runner,
    const std::filesystem::path &input,
    const std::filesystem::path &workspace) {
    std::filesystem::create_directories(workspace);
    const auto output = workspace / "audio.wav";
    const auto process = runner.run({
        "ffmpeg",
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        input.string(),
        "-vn",
        "-ac",
        "1",
        "-ar",
        "16000",
        "-c:a",
        "pcm_s16le",
        output.string(),
    });
    if (process.exit_code != 0) {
        throw std::runtime_error(
            "FFmpeg audio extraction failed: " + process.stderr_text);
    }
    if (!std::filesystem::is_regular_file(output) ||
        std::filesystem::file_size(output) == 0U) {
        throw std::runtime_error("FFmpeg did not produce extracted audio");
    }
    return output;
}

}  // namespace v2doc

