#include "v2doc/diarizer.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "sherpa-onnx/c-api/c-api.h"

namespace v2doc {
namespace {

struct DiarizerDeleter {
    void operator()(
        const SherpaOnnxOfflineSpeakerDiarization *diarizer) const {
        SherpaOnnxDestroyOfflineSpeakerDiarization(diarizer);
    }
};

struct ResultDeleter {
    void operator()(
        const SherpaOnnxOfflineSpeakerDiarizationResult *result) const {
        SherpaOnnxOfflineSpeakerDiarizationDestroyResult(result);
    }
};

struct SegmentsDeleter {
    void operator()(
        const SherpaOnnxOfflineSpeakerDiarizationSegment *segments) const {
        SherpaOnnxOfflineSpeakerDiarizationDestroySegment(segments);
    }
};

int inference_threads() {
    const auto available = std::thread::hardware_concurrency();
    return static_cast<int>(
        std::min(available == 0U ? 1U : available, 8U));
}

std::int64_t milliseconds(const float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0F) {
        throw std::runtime_error(
            "speaker diarization returned an invalid timestamp");
    }
    return static_cast<std::int64_t>(
        std::llround(static_cast<double>(seconds) * 1000.0));
}

}  // namespace

std::vector<SpeakerInterval> SherpaDiarizer::diarize(
    const AudioBuffer &audio, const AppOptions &options) const {
    if (audio.sample_rate != 16000) {
        throw std::invalid_argument(
            "speaker diarization requires 16000 Hz audio");
    }
    if (audio.samples.empty()) {
        throw std::invalid_argument(
            "speaker diarization cannot process empty audio");
    }
    if (audio.samples.size() >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error(
            "audio is too long for speaker diarization");
    }

    const auto segmentation =
        options.diarization_models /
        "sherpa-onnx-pyannote-segmentation-3-0" / "model.onnx";
    const auto embedding =
        options.diarization_models /
        "3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx";
    if (!std::filesystem::is_regular_file(segmentation)) {
        throw std::runtime_error(
            "diarization segmentation model does not exist: " +
            segmentation.string());
    }
    if (!std::filesystem::is_regular_file(embedding)) {
        throw std::runtime_error(
            "diarization embedding model does not exist: " +
            embedding.string());
    }

    const auto segmentation_string = segmentation.string();
    const auto embedding_string = embedding.string();
    SherpaOnnxOfflineSpeakerDiarizationConfig configuration{};
    configuration.segmentation.pyannote.model =
        segmentation_string.c_str();
    configuration.segmentation.num_threads = inference_threads();
    configuration.segmentation.provider = "cpu";
    configuration.embedding.model = embedding_string.c_str();
    configuration.embedding.num_threads = inference_threads();
    configuration.embedding.provider = "cpu";
    if (options.speaker_count.has_value()) {
        configuration.clustering.num_clusters =
            *options.speaker_count;
    } else {
        configuration.clustering.num_clusters = -1;
        configuration.clustering.threshold = 0.5F;
    }

    std::unique_ptr<
        const SherpaOnnxOfflineSpeakerDiarization,
        DiarizerDeleter>
        diarizer{
            SherpaOnnxCreateOfflineSpeakerDiarization(&configuration)};
    if (!diarizer) {
        throw std::runtime_error(
            "unable to initialize speaker diarization");
    }

    std::unique_ptr<
        const SherpaOnnxOfflineSpeakerDiarizationResult,
        ResultDeleter>
        result{SherpaOnnxOfflineSpeakerDiarizationProcess(
            diarizer.get(),
            audio.samples.data(),
            static_cast<std::int32_t>(audio.samples.size()))};
    if (!result) {
        throw std::runtime_error("speaker diarization failed");
    }

    const auto count =
        SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments(
            result.get());
    std::unique_ptr<
        const SherpaOnnxOfflineSpeakerDiarizationSegment,
        SegmentsDeleter>
        segments{
            SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime(
                result.get())};
    if (count > 0 && !segments) {
        throw std::runtime_error(
            "speaker diarization returned no segment data");
    }

    std::vector<SpeakerInterval> intervals;
    intervals.reserve(static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        intervals.push_back({
            milliseconds(segments.get()[index].start),
            milliseconds(segments.get()[index].end),
            segments.get()[index].speaker,
        });
    }
    return intervals;
}

}  // namespace v2doc

