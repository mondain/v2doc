#pragma once

#include "v2doc/types.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace v2doc {

std::string format_timestamp(std::int64_t milliseconds);

void write_report(
    const std::filesystem::path &directory,
    std::string_view input_name,
    std::string_view language,
    std::span<const TranscriptBlock> blocks,
    std::span<const std::string> warnings = {});

}  // namespace v2doc

