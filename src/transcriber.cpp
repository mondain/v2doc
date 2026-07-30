#include "v2doc/transcriber.hpp"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <whisper.h>

namespace v2doc {
namespace {

struct WhisperDeleter {
    void operator()(whisper_context *context) const {
        whisper_free(context);
    }
};

int inference_threads() {
    const auto available = std::thread::hardware_concurrency();
    return static_cast<int>(
        std::clamp(available == 0U ? 1U : available, 1U, 8U));
}

}  // namespace

TranscriptResult WhisperTranscriber::transcribe(
    const AudioBuffer &audio, const AppOptions &options) const {
    if (audio.sample_rate != 16000) {
        throw std::invalid_argument("Whisper requires 16000 Hz audio");
    }
    if (audio.samples.empty()) {
        throw std::invalid_argument("Whisper cannot transcribe empty audio");
    }
    if (!std::filesystem::is_regular_file(options.whisper_model)) {
        throw std::runtime_error(
            "Whisper model does not exist: " +
            options.whisper_model.string());
    }
    if (audio.samples.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::runtime_error("audio is too long for Whisper");
    }

    auto context_parameters = whisper_context_default_params();
    context_parameters.use_gpu = false;
    context_parameters.flash_attn = true;
    std::unique_ptr<whisper_context, WhisperDeleter> context{
        whisper_init_from_file_with_params(
            options.whisper_model.c_str(), context_parameters)};
    if (!context) {
        throw std::runtime_error("unable to load Whisper model");
    }

    auto parameters =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    parameters.n_threads = inference_threads();
    parameters.translate = false;
    parameters.no_timestamps = false;
    parameters.token_timestamps = true;
    parameters.split_on_word = true;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;
    const std::string language =
        options.language.value_or("auto");
    parameters.language = language.c_str();
    parameters.detect_language = !options.language.has_value();

    const auto status = whisper_full(
        context.get(),
        parameters,
        audio.samples.data(),
        static_cast<int>(audio.samples.size()));
    if (status != 0) {
        throw std::runtime_error(
            "Whisper transcription failed with status " +
            std::to_string(status));
    }

    std::vector<InferenceToken> tokens;
    const auto segment_count = whisper_full_n_segments(context.get());
    for (int segment = 0; segment < segment_count; ++segment) {
        const auto segment_start =
            whisper_full_get_segment_t0(context.get(), segment) * 10;
        const auto segment_end =
            whisper_full_get_segment_t1(context.get(), segment) * 10;
        const auto token_count =
            whisper_full_n_tokens(context.get(), segment);
        for (int index = 0; index < token_count; ++index) {
            const auto token =
                whisper_full_get_token_data(context.get(), segment, index);
            const bool special =
                token.id >= whisper_token_eot(context.get());
            auto start_ms = token.t0 * 10;
            auto end_ms = token.t1 * 10;
            if (!special && (start_ms < 0 || end_ms < start_ms)) {
                start_ms = segment_start;
                end_ms = segment_end;
            }
            tokens.push_back({
                start_ms,
                end_ms,
                whisper_token_to_str(context.get(), token.id),
                special,
            });
        }
    }

    const auto language_id = whisper_full_lang_id(context.get());
    const char *const detected = whisper_lang_str(language_id);
    return {
        detected == nullptr ? language : std::string{detected},
        tokens_to_words(tokens),
    };
}

}  // namespace v2doc

