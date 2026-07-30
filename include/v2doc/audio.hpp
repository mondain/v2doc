#pragma once

#include "v2doc/process.hpp"
#include "v2doc/types.hpp"

#include <filesystem>

namespace v2doc {

AudioBuffer read_pcm16_wave(const std::filesystem::path &path);

std::filesystem::path extract_audio(
    const ProcessRunner &runner,
    const std::filesystem::path &input,
    const std::filesystem::path &workspace);

}  // namespace v2doc

