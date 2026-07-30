#pragma once

#include "v2doc/process.hpp"
#include "v2doc/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace v2doc {

class Thumbnailer {
public:
    virtual ~Thumbnailer() = default;

    virtual std::vector<std::string> create(
        const std::filesystem::path &video,
        const std::filesystem::path &report_staging,
        std::vector<TranscriptBlock> &blocks,
        int width) const = 0;
};

class FfmpegThumbnailer final : public Thumbnailer {
public:
    explicit FfmpegThumbnailer(const ProcessRunner &runner);

    std::vector<std::string> create(
        const std::filesystem::path &video,
        const std::filesystem::path &report_staging,
        std::vector<TranscriptBlock> &blocks,
        int width) const override;

private:
    const ProcessRunner &runner_;
};

}  // namespace v2doc

