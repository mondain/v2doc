#include "v2doc/thumbnailer.hpp"

#include "v2doc/report.hpp"

#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace v2doc {
namespace {

std::string frame_name(const std::size_t index) {
    std::ostringstream name;
    name << "segment-" << std::setfill('0') << std::setw(4) << index
         << ".jpg";
    return name.str();
}

std::string seconds(const std::int64_t milliseconds) {
    std::ostringstream formatted;
    formatted.imbue(std::locale::classic());
    formatted << std::fixed << std::setprecision(3)
              << static_cast<double>(milliseconds) / 1000.0;
    return formatted.str();
}

}  // namespace

FfmpegThumbnailer::FfmpegThumbnailer(const ProcessRunner &runner)
    : runner_(runner) {}

std::vector<std::string> FfmpegThumbnailer::create(
    const std::filesystem::path &video,
    const std::filesystem::path &report_staging,
    std::vector<TranscriptBlock> &blocks,
    const int width) const {
    if (width <= 0) {
        throw std::invalid_argument(
            "thumbnail width must be greater than zero");
    }

    const auto thumbnail_directory = report_staging / "thumbnails";
    std::filesystem::create_directories(thumbnail_directory);
    std::vector<std::string> warnings;

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto &block = blocks[index];
        block.thumbnail.reset();
        const auto midpoint =
            block.start_ms + (block.end_ms - block.start_ms) / 2;
        const auto relative =
            std::filesystem::path{"thumbnails"} / frame_name(index + 1U);
        const auto output = report_staging / relative;

        const auto result = runner_.run({
            "ffmpeg",
            "-nostdin",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-ss",
            seconds(midpoint),
            "-i",
            video.string(),
            "-frames:v",
            "1",
            "-vf",
            "scale=" + std::to_string(width) + ":-2",
            "-q:v",
            "3",
            output.string(),
        });

        const bool produced =
            result.exit_code == 0 &&
            std::filesystem::is_regular_file(output) &&
            std::filesystem::file_size(output) > 0U;
        if (produced) {
            block.thumbnail = relative;
            continue;
        }

        std::string warning =
            "Thumbnail unavailable at " + format_timestamp(block.start_ms);
        if (!result.stderr_text.empty()) {
            warning += ": " + result.stderr_text;
        }
        warnings.push_back(std::move(warning));
    }

    return warnings;
}

}  // namespace v2doc

