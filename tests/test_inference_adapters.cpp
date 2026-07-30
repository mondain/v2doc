#include "v2doc/diarizer.hpp"
#include "v2doc/transcriber.hpp"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

namespace {

v2doc::AppOptions options_with_missing_models() {
    v2doc::AppOptions options;
    options.whisper_model = "/definitely/missing/ggml-small.bin";
    options.diarization_models = "/definitely/missing/diarization";
    return options;
}

}  // namespace

TEST_CASE("Whisper adapter validates audio before loading a model") {
    v2doc::WhisperTranscriber transcriber;
    auto options = options_with_missing_models();

    CHECK_THROWS_WITH(
        transcriber.transcribe(v2doc::AudioBuffer{44100, {0.0F}}, options),
        "Whisper requires 16000 Hz audio");
    CHECK_THROWS_WITH(
        transcriber.transcribe(v2doc::AudioBuffer{16000, {}}, options),
        "Whisper cannot transcribe empty audio");
}

TEST_CASE("Whisper adapter reports a missing model") {
    v2doc::WhisperTranscriber transcriber;
    const auto options = options_with_missing_models();

    CHECK_THROWS_WITH(
        transcriber.transcribe(v2doc::AudioBuffer{16000, {0.0F}}, options),
        "Whisper model does not exist: "
        "/definitely/missing/ggml-small.bin");
}

TEST_CASE("sherpa adapter validates audio and model files") {
    v2doc::SherpaDiarizer diarizer;
    const auto options = options_with_missing_models();

    CHECK_THROWS_WITH(
        diarizer.diarize(v2doc::AudioBuffer{44100, {0.0F}}, options),
        "speaker diarization requires 16000 Hz audio");
    CHECK_THROWS_WITH(
        diarizer.diarize(v2doc::AudioBuffer{16000, {0.0F}}, options),
        "diarization segmentation model does not exist: "
        "/definitely/missing/diarization/"
        "sherpa-onnx-pyannote-segmentation-3-0/model.onnx");
}

