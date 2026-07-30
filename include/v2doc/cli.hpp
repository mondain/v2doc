#pragma once

#include "v2doc/types.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace v2doc {

struct CliResult {
    std::optional<AppOptions> options;
    bool show_help{false};
    std::string error;
};

std::filesystem::path default_output_path(
    const std::filesystem::path &input);
std::filesystem::path default_model_root();
CliResult parse_cli(std::span<const std::string_view> args);

}  // namespace v2doc

