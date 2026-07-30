#include "v2doc/segment_merger.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

using v2doc::BlockRules;
using v2doc::SpeakerInterval;
using v2doc::TimedWord;
using v2doc::merge_transcript;

TEST_CASE("word midpoint selects a speaker and labels first appearance") {
    const std::vector words{
        TimedWord{0, 400, "Hello"},
        TimedWord{450, 900, "there."},
        TimedWord{1100, 1500, "Hi"},
    };
    const std::vector speakers{
        SpeakerInterval{0, 1000, 42},
        SpeakerInterval{1000, 2000, 7},
    };

    const auto blocks = merge_transcript(words, speakers);

    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].speaker == "speaker-1");
    CHECK(blocks[0].text == "Hello there.");
    CHECK(blocks[0].start_ms == 0);
    CHECK(blocks[0].end_ms == 900);
    CHECK(blocks[1].speaker == "speaker-2");
    CHECK(blocks[1].text == "Hi");
}

TEST_CASE("a returning cluster keeps its first assigned speaker label") {
    const std::vector words{
        TimedWord{0, 300, "One."},
        TimedWord{400, 700, "Two."},
        TimedWord{800, 1100, "Three."},
    };
    const std::vector speakers{
        SpeakerInterval{0, 350, 10},
        SpeakerInterval{350, 750, 20},
        SpeakerInterval{750, 1200, 10},
    };

    const auto blocks = merge_transcript(words, speakers);

    REQUIRE(blocks.size() == 3);
    CHECK(blocks[0].speaker == "speaker-1");
    CHECK(blocks[1].speaker == "speaker-2");
    CHECK(blocks[2].speaker == "speaker-1");
}

TEST_CASE("uncovered words use speaker unknown") {
    const std::vector words{TimedWord{100, 200, "Unassigned"}};

    const auto blocks = merge_transcript(words, {});

    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].speaker == "speaker-unknown");
}

TEST_CASE("greatest overlap resolves competing speaker intervals") {
    const std::vector words{
        TimedWord{0, 50, "Lead."},
        TimedWord{100, 900, "Overlap"},
    };
    const std::vector speakers{
        SpeakerInterval{0, 550, 1},
        SpeakerInterval{300, 1000, 2},
    };

    const auto blocks = merge_transcript(words, speakers);

    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].speaker == "speaker-1");
    CHECK(blocks[1].speaker == "speaker-2");
    CHECK(blocks[1].text == "Overlap");
}

TEST_CASE("sentence silence duration and speaker changes split blocks") {
    const std::vector words{
        TimedWord{0, 200, "First."},
        TimedWord{250, 500, "Second"},
        TimedWord{1600, 1800, "third"},
        TimedWord{1850, 2100, "speaker"},
    };
    const std::vector speakers{
        SpeakerInterval{0, 1800, 4},
        SpeakerInterval{1800, 2500, 5},
    };

    const auto blocks = merge_transcript(words, speakers);

    REQUIRE(blocks.size() == 4);
    CHECK(blocks[0].text == "First.");
    CHECK(blocks[1].text == "Second");
    CHECK(blocks[2].text == "third");
    CHECK(blocks[3].text == "speaker");
}

TEST_CASE("maximum block duration starts a new block") {
    const std::vector words{
        TimedWord{0, 5000, "Long"},
        TimedWord{5100, 10000, "running"},
        TimedWord{10100, 15100, "statement"},
    };
    const std::vector speakers{SpeakerInterval{0, 20000, 1}};

    const auto blocks =
        merge_transcript(words, speakers, BlockRules{1000, 15000});

    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].text == "Long running");
    CHECK(blocks[1].text == "statement");
}

TEST_CASE("punctuation and CJK tokens are joined readably") {
    const std::vector words{
        TimedWord{0, 100, "Hello"},
        TimedWord{100, 200, ","},
        TimedWord{200, 300, "world"},
        TimedWord{300, 400, "!"},
        TimedWord{500, 600, "你"},
        TimedWord{600, 700, "好"},
    };
    const std::vector speakers{SpeakerInterval{0, 1000, 1}};

    const auto blocks = merge_transcript(words, speakers);

    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].text == "Hello, world!");
    CHECK(blocks[1].text == "你好");
}

TEST_CASE("input order does not affect chronological output") {
    const std::vector words{
        TimedWord{300, 400, "second"},
        TimedWord{100, 200, "first"},
    };
    const std::vector speakers{
        SpeakerInterval{250, 500, 2},
        SpeakerInterval{0, 250, 1},
    };

    const auto blocks = merge_transcript(words, speakers);

    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].text == "first");
    CHECK(blocks[1].text == "second");
}

TEST_CASE("empty words are ignored") {
    const std::vector words{
        TimedWord{0, 100, " \t"},
        TimedWord{100, 200, "kept"},
    };

    const auto blocks = merge_transcript(words, {});

    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "kept");
}

TEST_CASE("negative and reversed timestamps are rejected") {
    const std::array negative{TimedWord{-1, 2, "bad"}};
    CHECK_THROWS_WITH(
        merge_transcript(negative, {}),
        "word timestamps must be nonnegative and ordered");

    const std::array reversed{TimedWord{2, 1, "bad"}};
    CHECK_THROWS_WITH(
        merge_transcript(reversed, {}),
        "word timestamps must be nonnegative and ordered");

    const std::array valid_word{TimedWord{0, 1, "word"}};
    const std::array invalid_speaker{SpeakerInterval{5, 4, 1}};
    CHECK_THROWS_WITH(
        merge_transcript(valid_word, invalid_speaker),
        "speaker timestamps must be nonnegative and ordered");
}
