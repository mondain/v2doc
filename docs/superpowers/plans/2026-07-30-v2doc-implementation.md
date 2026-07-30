# v2doc Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Linux C++20 CLI that turns a video file into a fully local, static HTML transcript report with timestamps, anonymous speaker labels, and representative thumbnails.

**Architecture:** A small dependency-free core owns command parsing, media-domain types, speaker/time merging, output transactions, and report rendering. Linux process execution calls FFmpeg safely with argument vectors, while directly linked whisper.cpp and sherpa-onnx adapters perform transcription and diarization. Interfaces around inference and thumbnail extraction keep the pipeline deterministic under unit and integration tests.

**Tech Stack:** C++20, CMake 3.24+, GCC 13, FFmpeg CLI, whisper.cpp v1.9.1 (`f049fff95a089aa9969deb009cdd4892b3e74916`), sherpa-onnx v1.12.40 (`d795ccd164bd2360ffda6c3431b1316e2c898be2`), nlohmann/json v3.12.0, Catch2 v3.15.0.

## Global Constraints

- Linux x86-64 and CPU inference are the only initially verified platform and backend.
- Use C++20 throughout and compile project targets with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.
- Processing must be offline after free models have been downloaded.
- Do not add live streaming, MoQ, cloud APIs, named-person recognition, a GUI, or overlapping-voice source separation.
- Default output for `/path/name.mp4` is `./name-transcript/`.
- The report contains no CDN, external script, font, or network request.
- Existing nonempty directories without a valid `.v2doc-report` marker are never removed, including with `--force`.
- Execute FFmpeg through an argument vector, never through a shell command assembled from user input.
- Preserve UTF-8 transcript text and escape it independently for HTML and JSON.
- Do not add a Markdown file beyond the approved design and implementation plan.
- Do not add a Codex author tagline, generated-by footer, emoji, or co-author line to commits.

## Planned File Structure

```text
CMakeLists.txt
.gitignore
include/v2doc/
    audio.hpp
    cli.hpp
    diarizer.hpp
    output_transaction.hpp
    pipeline.hpp
    process.hpp
    report.hpp
    segment_merger.hpp
    thumbnailer.hpp
    transcriber.hpp
    types.hpp
src/
    audio.cpp
    cli.cpp
    diarizer.cpp
    main.cpp
    output_transaction.cpp
    pipeline.cpp
    process.cpp
    report.cpp
    segment_merger.cpp
    thumbnailer.cpp
    transcriber.cpp
tests/
    CMakeLists.txt
    test_audio.cpp
    test_cli.cpp
    test_output_transaction.cpp
    test_pipeline.cpp
    test_process.cpp
    test_report.cpp
    test_segment_merger.cpp
    test_thumbnailer.cpp
scripts/
    download-models.sh
docs/superpowers/
    specs/2026-07-30-v2doc-design.md
    plans/2026-07-30-v2doc-implementation.md
```

---

### Task 1: Project Skeleton, Domain Types, and CLI

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `include/v2doc/types.hpp`
- Create: `include/v2doc/cli.hpp`
- Create: `src/cli.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_cli.cpp`

**Interfaces:**
- Produces:
  - `struct v2doc::AppOptions`
  - `struct v2doc::CliResult`
  - `CliResult v2doc::parse_cli(std::span<const std::string_view> args)`
  - `std::filesystem::path v2doc::default_output_path(const std::filesystem::path &input)`
  - `std::filesystem::path v2doc::default_model_root()`
  - Shared types `AudioBuffer`, `TimedWord`, `SpeakerInterval`, `TranscriptBlock`, and `TranscriptResult`

- [ ] **Step 1: Write CLI tests that fail because the project targets and functions do not exist**

```cpp
TEST_CASE("default output uses the input stem in the current directory") {
    CHECK(default_output_path("/media/interview.final.mp4") ==
          std::filesystem::path{"interview.final-transcript"});
}

TEST_CASE("CLI defaults to multilingual auto detection and 320 pixel thumbnails") {
    const std::array args{"v2doc"sv, "movie.mp4"sv};
    const auto result = parse_cli(args);
    REQUIRE(result.options.has_value());
    CHECK(result.options->language == std::nullopt);
    CHECK(result.options->thumbnail_width == 320);
    CHECK(result.options->diarization);
    CHECK_FALSE(result.options->force);
}

TEST_CASE("CLI rejects zero speakers and an empty output value") {
    const std::array zero{"v2doc"sv, "movie.mp4"sv, "--speakers"sv, "0"sv};
    CHECK(parse_cli(zero).error == "--speakers must be greater than zero");

    const std::array missing{"v2doc"sv, "movie.mp4"sv, "--output"sv};
    CHECK(parse_cli(missing).error == "--output requires a directory");
}
```

Also cover `--help`, `--language`, `--model`, `--diarization-models`,
`--thumbnail-width`, `--no-diarization`, `--force`, multiple input files,
unknown options, hidden filenames, Unicode filenames, and a filename with no
extension.

- [ ] **Step 2: Configure and run the test target to observe the intended failure**

Run:

```bash
cmake -S . -B build/core -DV2DOC_ENABLE_INFERENCE=OFF -DBUILD_TESTING=ON
cmake --build build/core -j
ctest --test-dir build/core --output-on-failure
```

Expected: configuration or compilation fails because `v2doc_core`,
`parse_cli`, and the domain types have not been created.

- [ ] **Step 3: Add the smallest CMake project, types, and parser that satisfy the tests**

Use these exact core fields:

```cpp
struct AppOptions {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path whisper_model;
    std::filesystem::path diarization_models;
    std::optional<std::string> language;
    std::optional<int> speaker_count;
    int thumbnail_width{320};
    bool diarization{true};
    bool force{false};
};

struct TimedWord {
    std::int64_t start_ms{};
    std::int64_t end_ms{};
    std::string text;
    bool operator==(const TimedWord &) const = default;
};

struct SpeakerInterval {
    std::int64_t start_ms{};
    std::int64_t end_ms{};
    int cluster{};
    bool operator==(const SpeakerInterval &) const = default;
};

struct TranscriptBlock {
    std::int64_t start_ms{};
    std::int64_t end_ms{};
    std::string speaker;
    std::string text;
    std::optional<std::filesystem::path> thumbnail;
};

struct TranscriptResult {
    std::string language;
    std::vector<TimedWord> words;
};
```

Resolve models from `V2DOC_MODEL_DIR`, then
`XDG_DATA_HOME/v2doc/models`, then `$HOME/.local/share/v2doc/models`.
Use `ggml-small.bin` and the diarization model directory under that root when
the matching CLI options are absent. If no safe home/data directory can be
resolved, leave the model paths empty so validation can produce a precise
error.

Fetch Catch2 only when `BUILD_TESTING=ON`. Keep inference dependencies behind
`V2DOC_ENABLE_INFERENCE`, which defaults to `ON`.

- [ ] **Step 4: Build and run the CLI tests**

Run:

```bash
cmake -S . -B build/core -DV2DOC_ENABLE_INFERENCE=OFF -DBUILD_TESTING=ON
cmake --build build/core -j
ctest --test-dir build/core -R cli --output-on-failure
```

Expected: the CLI test executable passes all cases.

- [ ] **Step 5: Commit the independently testable CLI foundation**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" init -b main
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add .gitignore CMakeLists.txt include/v2doc/types.hpp include/v2doc/cli.hpp src/cli.cpp tests/CMakeLists.txt tests/test_cli.cpp docs
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "build: establish v2doc C++20 CLI"
```

---

### Task 2: Safe Process Execution and Audio Extraction

**Files:**
- Create: `include/v2doc/process.hpp`
- Create: `src/process.cpp`
- Create: `include/v2doc/audio.hpp`
- Create: `src/audio.cpp`
- Create: `tests/test_process.cpp`
- Create: `tests/test_audio.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AudioBuffer` and `AppOptions` from `types.hpp`
- Produces:
  - `struct ProcessResult { int exit_code; std::string stderr_text; };`
  - `class ProcessRunner` with virtual
    `ProcessResult run(const std::vector<std::string> &args) const`
  - `class PosixProcessRunner final : public ProcessRunner`
  - `AudioBuffer read_pcm16_wave(const std::filesystem::path &path)`
  - `std::filesystem::path extract_audio(const ProcessRunner &, const std::filesystem::path &input, const std::filesystem::path &workspace)`

- [ ] **Step 1: Write failing process and WAV tests**

```cpp
TEST_CASE("process runner preserves arguments containing shell syntax") {
    PosixProcessRunner runner;
    const auto result = runner.run(
        {"/usr/bin/printf", "%s", "name;$(touch /tmp/v2doc-must-not-exist)"});
    CHECK(result.exit_code == 0);
    CHECK_FALSE(std::filesystem::exists("/tmp/v2doc-must-not-exist"));
}

TEST_CASE("PCM16 mono samples are normalized to floats") {
    const auto wave = write_test_wave({-32768, 0, 32767}, 16000, 1);
    const auto audio = read_pcm16_wave(wave);
    CHECK(audio.sample_rate == 16000);
    REQUIRE(audio.samples.size() == 3);
    CHECK(audio.samples[0] == Catch::Approx(-1.0F));
    CHECK(audio.samples[1] == Catch::Approx(0.0F));
    CHECK(audio.samples[2] == Catch::Approx(32767.0F / 32768.0F));
}
```

Add cases for an invalid RIFF signature, non-PCM encoding, stereo input,
truncated sample data, a missing `data` chunk, and an FFmpeg failure returned
by a fake `ProcessRunner`.

- [ ] **Step 2: Run the focused tests and confirm the missing implementation failure**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R "process|audio" --output-on-failure
```

Expected: compilation fails because the new interfaces do not exist.

- [ ] **Step 3: Implement Linux `fork`/`execvp` execution and strict WAV parsing**

`PosixProcessRunner` must:

1. Reject an empty argument vector.
2. create a `pipe2(..., O_CLOEXEC)` for stderr;
3. `fork`;
4. in the child, redirect stderr and call `execvp` using the supplied vector;
5. in the parent, read bounded stderr output, wait with `waitpid`, and convert
   signal termination to exit code `128 + signal`;
6. never invoke `/bin/sh`.

`extract_audio` must run this exact logical FFmpeg command:

```text
ffmpeg -nostdin -hide_banner -loglevel error -y
       -i INPUT -vn -ac 1 -ar 16000 -c:a pcm_s16le OUTPUT/audio.wav
```

The WAV reader accepts only mono, 16 kHz, 16-bit PCM and checks all chunk
lengths before reading. It returns normalized `float` samples.

- [ ] **Step 4: Run the focused and complete core suites**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R "process|audio" --output-on-failure
ctest --test-dir build/core --output-on-failure
```

Expected: all tests pass and `/tmp/v2doc-must-not-exist` does not exist.

- [ ] **Step 5: Commit process and audio support**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add CMakeLists.txt include/v2doc/process.hpp include/v2doc/audio.hpp src/process.cpp src/audio.cpp tests
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "feat: extract transcription audio safely"
```

---

### Task 3: Speaker Assignment and Readable Transcript Blocks

**Files:**
- Create: `include/v2doc/segment_merger.hpp`
- Create: `src/segment_merger.cpp`
- Create: `tests/test_segment_merger.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TimedWord`, `SpeakerInterval`, and `TranscriptBlock`
- Produces:
  - `struct BlockRules { std::int64_t silence_ms{1000}; std::int64_t max_duration_ms{15000}; };`
  - `std::vector<TranscriptBlock> merge_transcript(std::span<const TimedWord> words, std::span<const SpeakerInterval> speakers, BlockRules rules = {})`

- [ ] **Step 1: Write failing deterministic merger tests**

```cpp
TEST_CASE("word midpoint chooses a speaker and labels first appearance") {
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
    CHECK(blocks[1].speaker == "speaker-2");
}

TEST_CASE("uncovered words use speaker-unknown") {
    const std::vector words{TimedWord{100, 200, "Unassigned"}};
    const auto blocks = merge_transcript(words, {});
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].speaker == "speaker-unknown");
}
```

Also cover overlapping intervals, exact boundary midpoints, a speaker cluster
returning after another speaker, sentence punctuation, a silence longer than
one second, the 15-second maximum, invalid reversed timings, empty text, and
unsorted inputs.

- [ ] **Step 2: Run the merger tests and observe the missing-symbol failure**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R segment_merger --output-on-failure
```

Expected: compilation fails because `merge_transcript` is absent.

- [ ] **Step 3: Implement validated sorting, midpoint assignment, and block construction**

Use `start_ms + (end_ms - start_ms) / 2` to avoid midpoint overflow. If
multiple intervals cover the midpoint, select the interval with the greatest
overlap with the word, then the earliest interval, then the lowest cluster
number. Normalize nonnegative clusters into `speaker-N` in first-appearance
order. Never merge across a speaker change. Join English-like tokens with a
space unless the new token begins with closing punctuation; preserve
whitespace-free CJK tokens without inventing bytes.

Throw `std::invalid_argument` for negative or reversed times. Ignore words
whose trimmed text is empty.

- [ ] **Step 4: Run merger tests and the whole core suite**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R segment_merger --output-on-failure
ctest --test-dir build/core --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit the merger**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add CMakeLists.txt include/v2doc/segment_merger.hpp src/segment_merger.cpp tests
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "feat: assign speakers to transcript blocks"
```

---

### Task 4: Static Report Rendering and Safe Output Transactions

**Files:**
- Create: `include/v2doc/report.hpp`
- Create: `src/report.cpp`
- Create: `include/v2doc/output_transaction.hpp`
- Create: `src/output_transaction.cpp`
- Create: `tests/test_report.cpp`
- Create: `tests/test_output_transaction.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TranscriptBlock`
- Produces:
  - `std::string format_timestamp(std::int64_t milliseconds)`
  - `void write_report(const std::filesystem::path &directory, std::string_view input_name, std::string_view language, std::span<const TranscriptBlock> blocks, std::span<const std::string> warnings)`
  - `class OutputTransaction` with `staging_path()`, `commit()`, and scoped cleanup
  - `bool is_v2doc_report(const std::filesystem::path &directory)`

- [ ] **Step 1: Write failing report and transaction tests**

```cpp
TEST_CASE("timestamp includes hours and milliseconds") {
    CHECK(format_timestamp(3'723'004) == "01:02:03.004");
}

TEST_CASE("report escapes transcript text and stays offline") {
    const std::vector blocks{
        TranscriptBlock{0, 1000, "speaker-1", "<script>alert(\"x\")</script>", "thumbnails/segment-0001.jpg"}
    };
    write_report(temp.path(), "movie & test.mp4", "日本語", blocks, {});
    const auto html = read_file(temp.path() / "index.html");
    CHECK(html.find("<script>alert") == std::string::npos);
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    CHECK(html.find("https://") == std::string::npos);
    CHECK(html.find("http://") == std::string::npos);
}

TEST_CASE("force refuses an unrelated nonempty directory") {
    write_file(target / "personal.txt", "keep");
    CHECK_THROWS_AS(OutputTransaction(target, true), std::runtime_error);
    CHECK(read_file(target / "personal.txt") == "keep");
}
```

Also verify JSON millisecond values, valid UTF-8 round trips, plain-text
formatting, a null thumbnail, warnings, all relative thumbnail paths, required
files, marker content `v2doc-report-v1\n`, no replacement without `--force`,
replacement of a marked report, and cleanup of an uncommitted staging
directory.

- [ ] **Step 2: Run focused tests to confirm they fail before implementation**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R "report|output_transaction" --output-on-failure
```

Expected: compilation fails for the missing report and transaction APIs.

- [ ] **Step 3: Implement embedded offline HTML and transactional output**

Fetch nlohmann/json v3.12.0 for deterministic JSON encoding. The HTML template
must embed its CSS and optional filtering JavaScript directly. Escape all text
nodes and attribute values. A missing thumbnail renders a high-contrast light
gray placeholder with dark text.

Create staging as a unique sibling of the final directory. On `commit()`:

1. write and validate `index.html`, `transcript.txt`, `transcript.json`, and
   `.v2doc-report`;
2. if the target exists, accept removal only when `force` is true and the
   marker is exactly `v2doc-report-v1\n`;
3. rename the staging directory into place;
4. leave the target untouched if any earlier operation fails.

- [ ] **Step 4: Run report, transaction, and complete core tests**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R "report|output_transaction" --output-on-failure
ctest --test-dir build/core --output-on-failure
```

Expected: all tests pass and unrelated directories remain unchanged.

- [ ] **Step 5: Commit report generation**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add CMakeLists.txt include/v2doc/report.hpp include/v2doc/output_transaction.hpp src/report.cpp src/output_transaction.cpp tests
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "feat: generate portable transcript reports"
```

---

### Task 5: Timestamp-Matched Thumbnails

**Files:**
- Create: `include/v2doc/thumbnailer.hpp`
- Create: `src/thumbnailer.cpp`
- Create: `tests/test_thumbnailer.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ProcessRunner`, `TranscriptBlock`
- Produces:
  - `class Thumbnailer` with virtual `std::vector<std::string> create(const std::filesystem::path &video, const std::filesystem::path &report_staging, std::vector<TranscriptBlock> &blocks, int width) const`
  - `class FfmpegThumbnailer final : public Thumbnailer`

- [ ] **Step 1: Write failing thumbnail argument and recovery tests**

```cpp
TEST_CASE("thumbnail extraction seeks to each block midpoint") {
    RecordingRunner runner;
    FfmpegThumbnailer thumbnails(runner);
    std::vector blocks{TranscriptBlock{1000, 3000, "speaker-1", "text", std::nullopt}};
    const auto warnings = thumbnails.create("input name.mp4", output, blocks, 320);
    REQUIRE(warnings.empty());
    CHECK(runner.calls[0] == std::vector<std::string>{
        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-ss", "2.000", "-i", "input name.mp4", "-frames:v", "1",
        "-vf", "scale=320:-2", "-q:v", "3",
        (output / "thumbnails/segment-0001.jpg").string()
    });
}
```

Add tests for sequential zero-padded names, a failed individual frame that
leaves `thumbnail == std::nullopt`, width validation, shell metacharacters in
the input name, and creation of the thumbnail directory.

- [ ] **Step 2: Run the focused test and confirm failure**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R thumbnailer --output-on-failure
```

Expected: compilation fails because `FfmpegThumbnailer` is absent.

- [ ] **Step 3: Implement one accurate fast-seek FFmpeg invocation per block**

Format midpoint seconds with `std::locale::classic()`, exactly three decimal
places, and no negative values. Set the relative path only after FFmpeg exits
successfully and the JPEG is a nonempty regular file. Return one warning per
failure while continuing with later blocks.

- [ ] **Step 4: Run thumbnail and complete core tests**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R thumbnailer --output-on-failure
ctest --test-dir build/core --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit thumbnail extraction**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add CMakeLists.txt include/v2doc/thumbnailer.hpp src/thumbnailer.cpp tests
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "feat: capture transcript thumbnails"
```

---

### Task 6: whisper.cpp and sherpa-onnx Inference Adapters

**Files:**
- Create: `include/v2doc/transcriber.hpp`
- Create: `src/transcriber.cpp`
- Create: `include/v2doc/diarizer.hpp`
- Create: `src/diarizer.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_inference_text.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AudioBuffer`, `TranscriptResult`, `SpeakerInterval`, `AppOptions`
- Produces:
  - `class Transcriber` with virtual `TranscriptResult transcribe(const AudioBuffer &, const AppOptions &) const`
  - `class WhisperTranscriber final : public Transcriber`
  - `class Diarizer` with virtual `std::vector<SpeakerInterval> diarize(const AudioBuffer &, const AppOptions &) const`
  - `class SherpaDiarizer final : public Diarizer`
  - `struct InferenceToken { std::int64_t start_ms; std::int64_t end_ms; std::string text; bool special; };`
  - `std::vector<TimedWord> tokens_to_words(std::span<const InferenceToken>)`

- [ ] **Step 1: Write failing inference-independent token assembly tests**

```cpp
TEST_CASE("tokens become timestamped words without corrupting UTF-8") {
    const std::vector tokens{
        InferenceToken{0, 300, " Hello"},
        InferenceToken{300, 500, ","},
        InferenceToken{500, 800, " world"},
        InferenceToken{800, 900, "!"},
    };
    const auto words = tokens_to_words(tokens);
    REQUIRE(words.size() == 2);
    CHECK(words[0] == TimedWord{0, 500, "Hello,"});
    CHECK(words[1] == TimedWord{500, 900, "world!"});
}
```

Add a CJK case whose UTF-8 bytes arrive across adjacent tokens, invalid
timestamps, special/empty tokens, leading punctuation, and tokens without
spaces.

- [ ] **Step 2: Run the inference text test and observe failure**

```bash
cmake --build build/core -j
ctest --test-dir build/core -R inference_text --output-on-failure
```

Expected: compilation fails because the token assembly API is absent.

- [ ] **Step 3: Implement token assembly and direct adapters**

For whisper.cpp:

- initialize with `whisper_init_from_file_with_params`;
- use CPU, flash attention when supported, and
  `max(1, min(hardware_concurrency, 8))` threads;
- pass `language = "auto"` with language detection unless overridden;
- enable token timestamps and word splitting;
- run `whisper_full`;
- read language with `whisper_full_lang_id`;
- read token data with `whisper_full_get_token_data`;
- convert whisper's 10 ms timestamp units to integer milliseconds;
- merge partial UTF-8 byte sequences before word assembly;
- use RAII to call `whisper_free`.

For sherpa-onnx:

- configure the pyannote segmentation model at
  `sherpa-onnx-pyannote-segmentation-3-0/model.onnx`;
- configure
  `3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx`;
- set provider to `cpu`;
- set `num_clusters` from `--speakers`, otherwise set threshold `0.5`;
- pass the shared 16 kHz float buffer directly;
- sort returned intervals by start time;
- round seconds to integer milliseconds;
- release the result, segment array, and diarizer with RAII wrappers.

Configure sherpa-onnx with Python, tests, checks, PortAudio, JNI, WebSocket,
binaries, TTS, and GPU disabled; keep its C API and speaker diarization
enabled. Link `whisper` and `sherpa-onnx-c-api` only into `v2doc_inference`.

- [ ] **Step 4: Build the full dependency configuration and run all model-free tests**

```bash
cmake -S . -B build/full -DV2DOC_ENABLE_INFERENCE=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/full -j
ctest --test-dir build/full --output-on-failure
```

Expected: the full executable and tests compile; model-independent tests pass
without downloading inference models.

- [ ] **Step 5: Commit the inference adapters**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add CMakeLists.txt include/v2doc/transcriber.hpp include/v2doc/diarizer.hpp src/transcriber.cpp src/diarizer.cpp tests
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "feat: add local transcription and diarization"
```

---

### Task 7: Pipeline, CLI Executable, and Deterministic Integration Test

**Files:**
- Create: `include/v2doc/pipeline.hpp`
- Create: `src/pipeline.cpp`
- Create: `src/main.cpp`
- Create: `tests/test_pipeline.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: all earlier interfaces
- Produces:
  - `struct PipelineResult { std::filesystem::path output; std::vector<std::string> warnings; };`
  - `class Pipeline` constructed from `ProcessRunner`, `Transcriber`, `Diarizer`, and `Thumbnailer`
  - `PipelineResult Pipeline::run(const AppOptions &) const`
  - executable `v2doc`

- [ ] **Step 1: Write failing end-to-end pipeline tests with fake inference**

```cpp
TEST_CASE("pipeline generates a complete marked report") {
    FakeTranscriber transcriber({
        "en", {{0, 400, "Hello"}, {450, 900, "world."}}
    });
    FakeDiarizer diarizer({{0, 1000, 9}});
    FakeThumbnailer thumbnails;
    Pipeline pipeline(runner, transcriber, diarizer, thumbnails);

    auto options = valid_options(input_video, output_directory);
    const auto result = pipeline.run(options);

    CHECK(result.output == output_directory);
    CHECK(read_file(output_directory / ".v2doc-report") == "v2doc-report-v1\n");
    CHECK(std::filesystem::exists(output_directory / "index.html"));
    CHECK(read_file(output_directory / "transcript.txt").find("speaker-1") != std::string::npos);
}
```

Add cases for derived output naming, input validation, missing FFmpeg, no audio,
missing Whisper model, missing diarization models, `--no-diarization`, a
recoverable diarizer exception, a recoverable thumbnail failure, transcription
failure cleanup, and `--force` safety.

- [ ] **Step 2: Run the pipeline test to confirm the implementation is absent**

```bash
cmake --build build/full -j
ctest --test-dir build/full -R pipeline --output-on-failure
```

Expected: compilation fails because `Pipeline` is absent.

- [ ] **Step 3: Implement pipeline orchestration and user-facing progress**

Run stages in this order:

1. validate input, output, FFmpeg availability, and model paths;
2. create a scoped temporary workspace;
3. extract and load audio once;
4. transcribe;
5. diarize, converting runtime errors into one warning;
6. merge words and speakers;
7. open an `OutputTransaction`;
8. capture thumbnails into staging;
9. write and validate the report;
10. commit the output transaction.

`main.cpp` prints concise stage progress to stderr, prints the final report path
to stdout, returns `0` on success, `2` for CLI errors, and `1` for processing
errors. `--help` returns `0` without validating models or input.

- [ ] **Step 4: Run all tests plus a deterministic FFmpeg MP4 integration**

The integration test generates a two-second color MP4 with a sine-wave audio
track, uses fake transcription and diarization, and real FFmpeg audio/frame
extraction:

```bash
cmake --build build/full -j
ctest --test-dir build/full --output-on-failure
```

Expected: all tests pass; the integration output contains a nonempty JPEG and
all report artifacts.

- [ ] **Step 5: Commit the complete application path**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add CMakeLists.txt include/v2doc/pipeline.hpp src/pipeline.cpp src/main.cpp tests
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "feat: assemble the offline video transcript pipeline"
```

---

### Task 8: Verified Free-Model Setup and Real Inference Smoke Test

**Files:**
- Create: `scripts/download-models.sh`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: model layout expected by `WhisperTranscriber` and `SherpaDiarizer`
- Produces: reusable model directory accepted by `V2DOC_MODEL_DIR`

- [ ] **Step 1: Write shell-level validation checks before the script exists**

Run:

```bash
bash -n scripts/download-models.sh
scripts/download-models.sh --help
```

Expected: both commands fail because the script has not been created.

- [ ] **Step 2: Implement an idempotent downloader with exact checksums**

The script accepts `--model-dir DIR`, defaults through the same XDG lookup as
the executable, downloads to temporary sibling files, verifies SHA-256, and
renames only verified assets.

Use these exact assets:

```text
https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin
sha256 1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b

https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-segmentation-models/sherpa-onnx-pyannote-segmentation-3-0.tar.bz2
sha256 24615ee884c897d9d2ba09bb4d30da6bb1b15e685065962db5b02e76e4996488

https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx
sha256 1a331345f04805badbb495c775a6ddffcdd1a732567d5ec8b3d5749e3c7a5e4b
```

Require `curl`, `sha256sum`, and `tar`; print the total expected download size
before starting. Reuse matching existing files and reject mismatched files.

- [ ] **Step 3: Validate script syntax and help without downloading models**

```bash
bash -n scripts/download-models.sh
scripts/download-models.sh --help
```

Expected: syntax passes and help describes the model location, downloads, and
offline processing guarantee.

- [ ] **Step 4: Download models and run a real model-backed MP4 smoke test**

Create a small MP4 by pairing the fetched whisper.cpp JFK sample WAV with a
generated color video:

```bash
ffmpeg -y -f lavfi -i color=c=0x1f2937:s=640x360:r=25 \
  -i build/full/_deps/whisper_cpp-src/samples/jfk.wav \
  -shortest -c:v libx264 -pix_fmt yuv420p -c:a aac /tmp/v2doc-jfk.mp4
scripts/download-models.sh --model-dir /tmp/v2doc-smoke-models
V2DOC_MODEL_DIR=/tmp/v2doc-smoke-models ./build/full/v2doc /tmp/v2doc-jfk.mp4 --output /tmp/v2doc-jfk-report
test -s /tmp/v2doc-jfk-report/index.html
test -s /tmp/v2doc-jfk-report/transcript.json
test -s /tmp/v2doc-jfk-report/thumbnails/segment-0001.jpg
```

Expected: exit code `0`, detected language `en`, at least one transcript block,
at least one speaker label or the documented `speaker-unknown` fallback, and a
nonempty thumbnail.

- [ ] **Step 5: Commit verified model setup**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" add CMakeLists.txt scripts/download-models.sh
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" commit -m "build: add verified local model setup"
```

---

### Task 9: Release-Quality Verification and Private Repository Publication

**Files:**
- Modify only files implicated by verification failures

**Interfaces:**
- Consumes: the complete source tree and temporary Git metadata
- Produces: a verified `main` branch and, if GitHub access permits, a private
  `mondain/v2doc` repository

- [ ] **Step 1: Run clean core and full builds**

```bash
cmake -S . -B build/verify-core -DV2DOC_ENABLE_INFERENCE=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/verify-core -j
ctest --test-dir build/verify-core --output-on-failure

cmake -S . -B build/verify-full -DV2DOC_ENABLE_INFERENCE=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/verify-full -j
ctest --test-dir build/verify-full --output-on-failure
```

Expected: both builds exit `0`; CTest reports zero failures.

- [ ] **Step 2: Run static repository checks**

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" diff --check
rg -n "https?://" /tmp/v2doc-jfk-report/index.html
rg -n "Codex|Co-Authored-By: Codex|Generated with" . --glob '!docs/superpowers/**'
```

Expected: `git diff --check` is clean; the generated HTML URL search has no
matches; the prohibited attribution search has no matches.

- [ ] **Step 3: Exercise CLI failure and safety paths**

Run `--help`, a nonexistent input, an MP4 with no audio, an existing unrelated
output directory with `--force`, and a valid report replacement with `--force`.
Expected: help returns `0`; invalid cases return nonzero; unrelated files remain
unchanged; only the marked report is replaced.

- [ ] **Step 4: Review implementation against every design requirement**

Read `docs/superpowers/specs/2026-07-30-v2doc-design.md` and record concrete
verification evidence for CLI behavior, output files, offline HTML, speaker
fallback, thumbnails, failures, cleanup, tests, Linux CPU scope, and excluded
features. Correct any uncovered requirement and rerun the affected tests.

- [ ] **Step 5: Create and push the private GitHub repository**

Create `mondain/v2doc` as private only after confirming it does not already
exist. Add it as `origin` to `/tmp/v2doc-git`, push `main`, and verify:

```bash
git --git-dir=/tmp/v2doc-git --work-tree="$PWD" rev-list --left-right --count origin/main...main
git ls-remote --heads origin main
```

Expected: ahead/behind is `0 0` and the remote `refs/heads/main` object matches
local `main`. Report repository creation, commit, push, and remote verification
as separate states.
