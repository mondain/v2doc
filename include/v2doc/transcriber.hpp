#pragma once

#include "v2doc/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace v2doc {

struct InferenceToken {
    std::int64_t start_ms{};
    std::int64_t end_ms{};
    std::string text;
    bool special{};
};

std::vector<TimedWord> tokens_to_words(
    std::span<const InferenceToken> tokens);

class Transcriber {
public:
    virtual ~Transcriber() = default;

    virtual TranscriptResult transcribe(
        const AudioBuffer &audio, const AppOptions &options) const = 0;
};

class WhisperTranscriber final : public Transcriber {
public:
    TranscriptResult transcribe(
        const AudioBuffer &audio,
        const AppOptions &options) const override;
};

}  // namespace v2doc

