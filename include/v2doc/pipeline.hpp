#pragma once

#include "v2doc/types.hpp"

#include <filesystem>
#include <vector>

namespace v2doc {

class Diarizer;
class ProcessRunner;
class Thumbnailer;
class Transcriber;

struct PipelineResult {
    std::filesystem::path output;
    std::vector<std::string> warnings;
};

class Pipeline {
public:
    Pipeline(
        const ProcessRunner &runner,
        const Transcriber &transcriber,
        const Diarizer &diarizer,
        const Thumbnailer &thumbnailer);

    PipelineResult run(const AppOptions &options) const;

private:
    const ProcessRunner &runner_;
    const Transcriber &transcriber_;
    const Diarizer &diarizer_;
    const Thumbnailer &thumbnailer_;
};

}  // namespace v2doc
