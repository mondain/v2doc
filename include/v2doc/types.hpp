#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace v2doc {

struct AudioBuffer {
    int sample_rate{};
    std::vector<float> samples;
};

struct TimedWord {
    std::int64_t start_ms{};
    std::int64_t end_ms{};
    std::string text;

    bool operator==(const TimedWord &) const = default;
};

struct SpeakerInterval {
    std::int64_t start_ms{};
    std::int64_t end_ms{};
    int cluster{};

    bool operator==(const SpeakerInterval &) const = default;
};

struct TranscriptBlock {
    std::int64_t start_ms{};
    std::int64_t end_ms{};
    std::string speaker;
    std::string text;
    std::optional<std::filesystem::path> thumbnail;
};

struct TranscriptResult {
    std::string language;
    std::vector<TimedWord> words;
};

struct AppOptions {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path whisper_model;
    std::filesystem::path diarization_models;
    std::optional<std::string> language;
    std::optional<int> speaker_count;
    int thumbnail_width{320};
    bool diarization{true};
    bool force{false};
};

}  // namespace v2doc

