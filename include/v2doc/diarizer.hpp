#pragma once

#include "v2doc/types.hpp"

#include <vector>

namespace v2doc {

class Diarizer {
public:
    virtual ~Diarizer() = default;

    virtual std::vector<SpeakerInterval> diarize(
        const AudioBuffer &audio, const AppOptions &options) const = 0;
};

class SherpaDiarizer final : public Diarizer {
public:
    std::vector<SpeakerInterval> diarize(
        const AudioBuffer &audio,
        const AppOptions &options) const override;
};

}  // namespace v2doc

