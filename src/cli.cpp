#include "v2doc/cli.hpp"

#include <charconv>
#include <cstdlib>
#include <system_error>

namespace v2doc {
namespace {

std::optional<int> parse_positive_integer(const std::string_view value) {
    int parsed{};
    const auto *const begin = value.data();
    const auto *const end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string_view> option_value(
    const std::span<const std::string_view> args, std::size_t &index) {
    if (index + 1 >= args.size()) {
        return std::nullopt;
    }
    ++index;
    return args[index];
}

std::filesystem::path environment_path(const char *const name) {
    const char *const value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return {};
    }
    return value;
}

}  // namespace

std::filesystem::path default_output_path(
    const std::filesystem::path &input) {
    const auto filename = input.filename();
    auto stem = filename.stem();
    if (stem.empty()) {
        stem = filename;
    }
    return stem.string() + "-transcript";
}

std::filesystem::path default_model_root() {
    if (const auto configured = environment_path("V2DOC_MODEL_DIR");
        !configured.empty()) {
        return configured;
    }
    if (const auto xdg_data = environment_path("XDG_DATA_HOME");
        !xdg_data.empty()) {
        return xdg_data / "v2doc" / "models";
    }
    if (const auto user_home = environment_path("HOME"); !user_home.empty()) {
        return user_home / ".local" / "share" / "v2doc" / "models";
    }
    return {};
}

CliResult parse_cli(const std::span<const std::string_view> args) {
    CliResult result;
    AppOptions options;
    bool has_input = false;

    const auto model_root = default_model_root();
    if (!model_root.empty()) {
        options.whisper_model = model_root / "ggml-small.bin";
        options.diarization_models = model_root;
    }

    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto argument = args[index];

        if (argument == "--help") {
            result.show_help = true;
            return result;
        }
        if (argument == "--force") {
            options.force = true;
            continue;
        }
        if (argument == "--no-diarization") {
            options.diarization = false;
            continue;
        }

        auto require_value = [&](const std::string &error)
            -> std::optional<std::string_view> {
            const auto value = option_value(args, index);
            if (!value.has_value() || value->empty()) {
                result.error = error;
                return std::nullopt;
            }
            return value;
        };

        if (argument == "--output") {
            const auto value =
                require_value("--output requires a directory");
            if (!value) {
                return result;
            }
            options.output = *value;
            continue;
        }
        if (argument == "--model") {
            const auto value = require_value("--model requires a file");
            if (!value) {
                return result;
            }
            options.whisper_model = *value;
            continue;
        }
        if (argument == "--diarization-models") {
            const auto value = require_value(
                "--diarization-models requires a directory");
            if (!value) {
                return result;
            }
            options.diarization_models = *value;
            continue;
        }
        if (argument == "--language") {
            const auto value =
                require_value("--language requires a language code");
            if (!value) {
                return result;
            }
            options.language = std::string{*value};
            continue;
        }
        if (argument == "--speakers") {
            const auto value =
                require_value("--speakers requires a positive integer");
            if (!value) {
                return result;
            }
            const auto parsed = parse_positive_integer(*value);
            if (!parsed) {
                result.error =
                    *value == "0"
                        ? "--speakers must be greater than zero"
                        : "--speakers requires a positive integer";
                return result;
            }
            options.speaker_count = *parsed;
            continue;
        }
        if (argument == "--thumbnail-width") {
            const auto value = require_value(
                "--thumbnail-width requires a positive integer");
            if (!value) {
                return result;
            }
            const auto parsed = parse_positive_integer(*value);
            if (!parsed) {
                result.error =
                    *value == "0"
                        ? "--thumbnail-width must be greater than zero"
                        : "--thumbnail-width requires a positive integer";
                return result;
            }
            options.thumbnail_width = *parsed;
            continue;
        }
        if (argument.starts_with('-')) {
            result.error = "unknown option: " + std::string{argument};
            return result;
        }
        if (has_input) {
            result.error = "only one input video is supported";
            return result;
        }
        options.input = argument;
        has_input = true;
    }

    if (!has_input) {
        result.error = "an input video is required";
        return result;
    }
    if (options.output.empty()) {
        options.output = default_output_path(options.input);
    }

    result.options = std::move(options);
    return result;
}

}  // namespace v2doc

