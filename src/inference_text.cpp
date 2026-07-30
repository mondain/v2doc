#include "v2doc/transcriber.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace v2doc {
namespace {

enum class Utf8Status {
    valid,
    incomplete,
    invalid,
};

Utf8Status utf8_status(const std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7fU) {
            ++index;
            continue;
        }

        std::size_t length{};
        std::uint32_t codepoint{};
        std::uint32_t minimum{};
        if ((lead & 0xe0U) == 0xc0U) {
            length = 2;
            codepoint = lead & 0x1fU;
            minimum = 0x80U;
        } else if ((lead & 0xf0U) == 0xe0U) {
            length = 3;
            codepoint = lead & 0x0fU;
            minimum = 0x800U;
        } else if ((lead & 0xf8U) == 0xf0U) {
            length = 4;
            codepoint = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return Utf8Status::invalid;
        }

        if (text.size() - index < length) {
            return Utf8Status::incomplete;
        }
        for (std::size_t continuation = 1; continuation < length;
             ++continuation) {
            const auto byte =
                static_cast<unsigned char>(text[index + continuation]);
            if ((byte & 0xc0U) != 0x80U) {
                return Utf8Status::invalid;
            }
            codepoint = (codepoint << 6U) | (byte & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return Utf8Status::invalid;
        }
        index += length;
    }
    return Utf8Status::valid;
}

std::vector<InferenceToken> merge_utf8_fragments(
    const std::span<const InferenceToken> tokens) {
    std::vector<InferenceToken> merged;
    std::optional<InferenceToken> pending;

    for (const auto &token : tokens) {
        if (token.start_ms < 0 || token.end_ms < token.start_ms) {
            throw std::invalid_argument(
                "token timestamps must be nonnegative and ordered");
        }
        if (token.special) {
            continue;
        }

        if (!pending.has_value()) {
            pending = token;
        } else {
            pending->text += token.text;
            pending->end_ms = token.end_ms;
        }

        switch (utf8_status(pending->text)) {
            case Utf8Status::valid:
                merged.push_back(std::move(*pending));
                pending.reset();
                break;
            case Utf8Status::incomplete:
                break;
            case Utf8Status::invalid:
                throw std::invalid_argument("invalid UTF-8 token sequence");
        }
    }

    if (pending.has_value()) {
        throw std::invalid_argument("incomplete UTF-8 token sequence");
    }
    return merged;
}

bool ascii_space(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

std::string_view trim_ascii(const std::string_view text) {
    auto begin = text.begin();
    auto end = text.end();
    while (begin != end && ascii_space(*begin)) {
        ++begin;
    }
    while (end != begin && ascii_space(*(end - 1))) {
        --end;
    }
    return {begin, end};
}

}  // namespace

std::vector<TimedWord> tokens_to_words(
    const std::span<const InferenceToken> tokens) {
    const auto normalized = merge_utf8_fragments(tokens);
    std::vector<TimedWord> words;
    TimedWord current;
    bool has_current = false;

    auto flush = [&] {
        if (has_current && !current.text.empty()) {
            words.push_back(std::move(current));
        }
        current = {};
        has_current = false;
    };

    for (const auto &token : normalized) {
        const bool begins_word =
            !token.text.empty() && ascii_space(token.text.front());
        const bool ends_word =
            !token.text.empty() && ascii_space(token.text.back());
        const auto text = trim_ascii(token.text);

        if (begins_word) {
            flush();
        }
        if (text.empty()) {
            if (ends_word) {
                flush();
            }
            continue;
        }

        if (!has_current) {
            current = {
                token.start_ms,
                token.end_ms,
                std::string{text},
            };
            has_current = true;
        } else {
            current.text.append(text);
            current.end_ms =
                std::max(current.end_ms, token.end_ms);
        }
        if (ends_word) {
            flush();
        }
    }
    flush();
    return words;
}

}  // namespace v2doc
