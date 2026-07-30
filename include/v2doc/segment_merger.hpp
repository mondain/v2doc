#pragma once

#include "v2doc/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace v2doc {

struct BlockRules {
    std::int64_t silence_ms{1000};
    std::int64_t max_duration_ms{15000};
};

std::vector<TranscriptBlock> merge_transcript(
    std::span<const TimedWord> words,
    std::span<const SpeakerInterval> speakers,
    BlockRules rules = {});

}  // namespace v2doc

