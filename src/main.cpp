#include "v2doc/cli.hpp"
#include "v2doc/diarizer.hpp"
#include "v2doc/pipeline.hpp"
#include "v2doc/process.hpp"
#include "v2doc/thumbnailer.hpp"
#include "v2doc/transcriber.hpp"

#include <exception>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view usage =
    "Usage: v2doc [options] VIDEO\n"
    "\n"
    "Create an offline, self-contained transcript report from a video file.\n"
    "\n"
    "Options:\n"
    "  --output DIR                Report directory\n"
    "  --model FILE                Multilingual whisper.cpp model\n"
    "  --diarization-models DIR    sherpa-onnx model directory\n"
    "  --language CODE             Language code (default: auto-detect)\n"
    "  --speakers N                Known number of speakers\n"
    "  --thumbnail-width N         Thumbnail width (default: 320)\n"
    "  --no-diarization            Do not identify speakers\n"
    "  --force                     Replace an existing v2doc report\n"
    "  --help                      Show this help\n";

}  // namespace

int main(const int argc, char *argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto parsed = v2doc::parse_cli(arguments);
    if (parsed.show_help) {
        std::cout << usage;
        return 0;
    }
    if (!parsed.options.has_value()) {
        std::cerr << "v2doc: " << parsed.error << "\n\n" << usage;
        return 2;
    }

    try {
        v2doc::PosixProcessRunner runner;
        v2doc::WhisperTranscriber transcriber;
        v2doc::SherpaDiarizer diarizer;
        v2doc::FfmpegThumbnailer thumbnails(runner);
        v2doc::Pipeline pipeline(
            runner, transcriber, diarizer, thumbnails);

        std::cerr << "Processing " << parsed.options->input << '\n';
        const auto result = pipeline.run(*parsed.options);
        for (const auto &warning : result.warnings) {
            std::cerr << "Warning: " << warning << '\n';
        }
        std::cout << result.output << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "v2doc: " << error.what() << '\n';
        return 1;
    }
}
