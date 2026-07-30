#include "v2doc/transcriber.hpp"

#include <array>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

using v2doc::InferenceToken;
using v2doc::TimedWord;
using v2doc::tokens_to_words;

TEST_CASE("tokens become timestamped words with attached punctuation") {
    const std::vector tokens{
        InferenceToken{0, 300, " Hello", false},
        InferenceToken{300, 500, ",", false},
        InferenceToken{500, 800, " world", false},
        InferenceToken{800, 900, "!", false},
    };

    const auto words = tokens_to_words(tokens);

    REQUIRE(words.size() == 2);
    CHECK(words[0] == TimedWord{0, 500, "Hello,"});
    CHECK(words[1] == TimedWord{500, 900, "world!"});
}

TEST_CASE("split UTF8 bytes are merged before word assembly") {
    const std::vector tokens{
        InferenceToken{0, 100, std::string{"\xE4", 1}, false},
        InferenceToken{100, 200, std::string{"\xBD\xA0", 2}, false},
        InferenceToken{200, 300, std::string{"\xE5\xA5", 2}, false},
        InferenceToken{300, 400, std::string{"\xBD", 1}, false},
    };

    const auto words = tokens_to_words(tokens);

    REQUIRE(words.size() == 1);
    CHECK(words[0] == TimedWord{0, 400, "你好"});
}

TEST_CASE("subword pieces without whitespace remain one word") {
    const std::vector tokens{
        InferenceToken{0, 100, " trans", false},
        InferenceToken{100, 200, "cript", false},
        InferenceToken{200, 300, "ion", false},
    };

    const auto words = tokens_to_words(tokens);

    REQUIRE(words.size() == 1);
    CHECK(words[0] == TimedWord{0, 300, "transcription"});
}

TEST_CASE("special and whitespace-only tokens are skipped") {
    const std::vector tokens{
        InferenceToken{0, 10, "<start>", true},
        InferenceToken{10, 20, "   ", false},
        InferenceToken{20, 100, " kept", false},
    };

    const auto words = tokens_to_words(tokens);

    REQUIRE(words.size() == 1);
    CHECK(words[0] == TimedWord{20, 100, "kept"});
}

TEST_CASE("invalid token timestamps and incomplete UTF8 are rejected") {
    const std::array reversed{
        InferenceToken{100, 50, " bad", false},
    };
    CHECK_THROWS_WITH(
        tokens_to_words(reversed),
        "token timestamps must be nonnegative and ordered");

    const std::array incomplete{
        InferenceToken{0, 100, std::string{"\xE4", 1}, false},
    };
    CHECK_THROWS_WITH(
        tokens_to_words(incomplete),
        "incomplete UTF-8 token sequence");
}

