#include "v2doc/pipeline.hpp"

#include "v2doc/audio.hpp"
#include "v2doc/cli.hpp"
#include "v2doc/diarizer.hpp"
#include "v2doc/output_transaction.hpp"
#include "v2doc/process.hpp"
#include "v2doc/report.hpp"
#include "v2doc/segment_merger.hpp"
#include "v2doc/thumbnailer.hpp"
#include "v2doc/transcriber.hpp"

#include <atomic>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace v2doc {
namespace {

class TemporaryWorkspace {
public:
    TemporaryWorkspace() {
        static std::atomic<unsigned long> next{0};
        const auto root = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 1000U; ++attempt) {
            const auto candidate =
                root /
                ("v2doc-work-" + std::to_string(::getpid()) + "-" +
                 std::to_string(next.fetch_add(1)));
            if (std::filesystem::create_directory(candidate)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("unable to create a temporary workspace");
    }

    ~TemporaryWorkspace() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryWorkspace(const TemporaryWorkspace &) = delete;
    TemporaryWorkspace &operator=(const TemporaryWorkspace &) = delete;

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string process_failure(
    const std::string &prefix, const ProcessResult &result) {
    if (!result.stderr_text.empty()) {
        return prefix + ": " + result.stderr_text;
    }
    return prefix + " (exit status " + std::to_string(result.exit_code) + ")";
}

void validate_options(
    const ProcessRunner &runner, const AppOptions &options) {
    if (!std::filesystem::is_regular_file(options.input)) {
        throw std::runtime_error("input video is not a regular file");
    }
    if (options.output.empty()) {
        throw std::runtime_error("output directory cannot be empty");
    }
    if (options.thumbnail_width <= 0) {
        throw std::runtime_error(
            "thumbnail width must be greater than zero");
    }

    const auto ffmpeg = runner.run({"ffmpeg", "-version"});
    if (ffmpeg.exit_code != 0) {
        throw std::runtime_error(process_failure(
            "FFmpeg is unavailable", ffmpeg));
    }
    if (!std::filesystem::is_regular_file(options.whisper_model)) {
        throw std::runtime_error("Whisper model is not a regular file");
    }
    if (!options.diarization) {
        return;
    }

    const auto segmentation =
        options.diarization_models /
        "sherpa-onnx-pyannote-segmentation-3-0" / "model.onnx";
    const auto embedding =
        options.diarization_models /
        "3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx";
    if (!std::filesystem::is_regular_file(segmentation) ||
        !std::filesystem::is_regular_file(embedding)) {
        throw std::runtime_error("diarization model files are missing");
    }
}

}  // namespace

Pipeline::Pipeline(
    const ProcessRunner &runner,
    const Transcriber &transcriber,
    const Diarizer &diarizer,
    const Thumbnailer &thumbnailer)
    : runner_(runner),
      transcriber_(transcriber),
      diarizer_(diarizer),
      thumbnailer_(thumbnailer) {}

PipelineResult Pipeline::run(const AppOptions &options) const {
    AppOptions normalized = options;
    if (normalized.output.empty()) {
        normalized.output = default_output_path(normalized.input);
    }
    validate_options(runner_, normalized);

    TemporaryWorkspace workspace;
    const auto wave =
        extract_audio(runner_, normalized.input, workspace.path());
    const auto audio = read_pcm16_wave(wave);
    if (audio.samples.empty()) {
        throw std::runtime_error("extracted audio is empty");
    }

    const auto transcript = transcriber_.transcribe(audio, normalized);
    std::vector<std::string> warnings;
    std::vector<SpeakerInterval> speakers;
    if (normalized.diarization) {
        try {
            speakers = diarizer_.diarize(audio, normalized);
        } catch (const std::exception &error) {
            warnings.push_back(
                "Speaker diarization unavailable: " +
                std::string{error.what()});
        }
    }

    auto blocks = merge_transcript(transcript.words, speakers);
    OutputTransaction output(normalized.output, normalized.force);
    auto thumbnail_warnings = thumbnailer_.create(
        normalized.input,
        output.staging_path(),
        blocks,
        normalized.thumbnail_width);
    warnings.insert(
        warnings.end(),
        std::make_move_iterator(thumbnail_warnings.begin()),
        std::make_move_iterator(thumbnail_warnings.end()));

    write_report(
        output.staging_path(),
        normalized.input.filename().string(),
        transcript.language,
        blocks,
        warnings);
    output.commit();

    return {normalized.output, std::move(warnings)};
}

}  // namespace v2doc
