#include "v2doc/segment_merger.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace v2doc {
namespace {

std::string trim_ascii(const std::string_view value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end &&
           std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }
    while (end != begin &&
           std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
        --end;
    }
    return {begin, end};
}

bool contains_non_ascii(const std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](const char character) {
        return static_cast<unsigned char>(character) >= 0x80U;
    });
}

bool starts_with_closing_punctuation(const std::string_view value) {
    if (value.empty()) {
        return false;
    }
    constexpr std::string_view punctuation = ",.;:!?)]}%";
    return punctuation.find(value.front()) != std::string_view::npos;
}

bool ends_sentence(const std::string_view value) {
    if (value.empty()) {
        return false;
    }
    constexpr std::string_view endings = ".!?";
    return endings.find(value.back()) != std::string_view::npos;
}

std::int64_t interval_overlap(
    const TimedWord &word, const SpeakerInterval &speaker) {
    const auto begin = std::max(word.start_ms, speaker.start_ms);
    const auto end = std::min(word.end_ms, speaker.end_ms);
    return std::max<std::int64_t>(0, end - begin);
}

std::optional<int> select_cluster(
    const TimedWord &word,
    const std::vector<SpeakerInterval> &speakers) {
    const auto midpoint =
        word.start_ms + (word.end_ms - word.start_ms) / 2;
    const SpeakerInterval *selected = nullptr;
    std::int64_t selected_overlap = std::numeric_limits<std::int64_t>::min();

    for (const auto &speaker : speakers) {
        if (midpoint < speaker.start_ms || midpoint >= speaker.end_ms) {
            continue;
        }
        const auto overlap = interval_overlap(word, speaker);
        if (selected == nullptr || overlap > selected_overlap ||
            (overlap == selected_overlap &&
             (speaker.start_ms < selected->start_ms ||
              (speaker.start_ms == selected->start_ms &&
               speaker.cluster < selected->cluster)))) {
            selected = &speaker;
            selected_overlap = overlap;
        }
    }

    if (selected == nullptr) {
        return std::nullopt;
    }
    return selected->cluster;
}

void append_word(std::string &text, const std::string_view word) {
    if (text.empty()) {
        text = word;
        return;
    }
    if (starts_with_closing_punctuation(word) ||
        (contains_non_ascii(text) && contains_non_ascii(word))) {
        text.append(word);
        return;
    }
    text.push_back(' ');
    text.append(word);
}

}  // namespace

std::vector<TranscriptBlock> merge_transcript(
    const std::span<const TimedWord> words,
    const std::span<const SpeakerInterval> speakers,
    const BlockRules rules) {
    if (rules.silence_ms < 0 || rules.max_duration_ms <= 0) {
        throw std::invalid_argument("block rules must use positive durations");
    }

    std::vector<TimedWord> ordered_words{words.begin(), words.end()};
    for (const auto &word : ordered_words) {
        if (word.start_ms < 0 || word.end_ms < word.start_ms) {
            throw std::invalid_argument(
                "word timestamps must be nonnegative and ordered");
        }
    }
    std::stable_sort(
        ordered_words.begin(),
        ordered_words.end(),
        [](const TimedWord &left, const TimedWord &right) {
            return std::tie(left.start_ms, left.end_ms) <
                   std::tie(right.start_ms, right.end_ms);
        });

    std::vector<SpeakerInterval> ordered_speakers{
        speakers.begin(), speakers.end()};
    for (const auto &speaker : ordered_speakers) {
        if (speaker.start_ms < 0 || speaker.end_ms < speaker.start_ms) {
            throw std::invalid_argument(
                "speaker timestamps must be nonnegative and ordered");
        }
    }
    std::stable_sort(
        ordered_speakers.begin(),
        ordered_speakers.end(),
        [](const SpeakerInterval &left, const SpeakerInterval &right) {
            return std::tie(left.start_ms, left.end_ms, left.cluster) <
                   std::tie(right.start_ms, right.end_ms, right.cluster);
        });

    std::map<int, std::string> labels;
    int next_label = 1;
    std::vector<TranscriptBlock> blocks;

    for (const auto &word : ordered_words) {
        const auto text = trim_ascii(word.text);
        if (text.empty()) {
            continue;
        }

        std::string speaker = "speaker-unknown";
        if (const auto cluster = select_cluster(word, ordered_speakers);
            cluster.has_value()) {
            auto [entry, inserted] =
                labels.try_emplace(*cluster, std::string{});
            if (inserted) {
                entry->second = "speaker-" + std::to_string(next_label++);
            }
            speaker = entry->second;
        }

        const bool needs_new_block =
            blocks.empty() || blocks.back().speaker != speaker ||
            word.start_ms - blocks.back().end_ms > rules.silence_ms ||
            word.end_ms - blocks.back().start_ms > rules.max_duration_ms ||
            ends_sentence(blocks.back().text);

        if (needs_new_block) {
            blocks.push_back({
                word.start_ms,
                word.end_ms,
                std::move(speaker),
                text,
                std::nullopt,
            });
            continue;
        }

        append_word(blocks.back().text, text);
        blocks.back().end_ms = std::max(blocks.back().end_ms, word.end_ms);
    }

    return blocks;
}

}  // namespace v2doc

