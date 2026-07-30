# v2doc Design

## Purpose

`v2doc` is a Linux command-line application that processes an existing video
file, transcribes its spoken audio locally, attempts to distinguish recurring
speakers, captures representative video frames, and writes a portable static
report. It does not process live streams and has no MoQ integration.

All inference runs locally after the user downloads free model files. The
application does not call paid services or require an Internet connection while
processing a video.

## Command-Line Interface

The basic invocation is:

```text
v2doc interview.mp4
```

If `--output` is omitted, the output directory is derived from the input
filename in the current working directory:

```text
/path/to/interview.mp4 -> ./interview-transcript/
```

An explicit output directory is supported:

```text
v2doc interview.mp4 --output /reports/interview
```

The first version supports these options:

- `--output DIR`: set the report directory.
- `--model FILE`: select a whisper.cpp model file.
- `--diarization-models DIR`: select the sherpa-onnx model directory.
- `--language CODE`: use a known language instead of automatic detection.
- `--speakers N`: supply a known speaker count instead of automatic clustering.
- `--thumbnail-width N`: set thumbnail width; the default is 320 pixels.
- `--no-diarization`: generate a transcript without speaker clustering.
- `--force`: replace files in an existing generated report directory.
- `--help`: show command usage.

The default transcription model is the multilingual Whisper `small` model,
chosen as a practical CPU accuracy and speed balance. The setup script downloads
the default model set once so models can be reused across reports.

The application refuses to overwrite an existing output directory unless
`--force` is present. Even with `--force`, a nonempty directory is replaceable
only when it contains the v2doc report marker written by an earlier successful
run. This prevents an output typo from deleting an unrelated directory.
`--force` never operates on the output directory's parent or an inferred broad
path.

## Output

Each run produces a static directory:

```text
interview-transcript/
|-- .v2doc-report
|-- index.html
|-- transcript.txt
|-- transcript.json
`-- thumbnails/
    |-- segment-0001.jpg
    `-- segment-0002.jpg
```

`index.html` contains the report markup, styles, and behavior without external
CDNs, scripts, fonts, or network requests. It refers only to files inside the
report directory and opens directly in a browser without a server.

Each visible transcript block contains:

- A 320-pixel-wide JPEG thumbnail by default.
- Start and end timestamps.
- An anonymous label such as `speaker-1`.
- Transcript text.

`transcript.txt` contains a readable timestamped transcript. `transcript.json`
contains the same blocks as structured UTF-8 data, including times in
milliseconds, speaker identifiers, text, and relative thumbnail paths.
`.v2doc-report` contains the report schema version and acts as the safety marker
for a later `--force` run.

## Architecture and Data Flow

The C++20 executable coordinates three local media and inference components:

1. FFmpeg extracts a temporary mono, 16 kHz PCM WAV file from the input video.
2. whisper.cpp detects the language and produces text with word timestamps.
3. sherpa-onnx produces time intervals associated with anonymous speaker
   clusters.
4. The segment merger assigns each word to the speaker interval covering its
   midpoint, then combines adjacent words into readable blocks without merging
   different speakers.
5. FFmpeg extracts a video frame near each completed block's midpoint and scales
   it to the configured thumbnail width while preserving aspect ratio.
6. The report writer generates HTML, JSON, and plain text in a staging
   directory. The completed staging directory becomes the selected output only
   after all required artifacts are valid.

FFmpeg remains an external runtime executable for the first version. The
application links whisper.cpp and sherpa-onnx directly to avoid intermediate
transcription or diarization processes.

## Speaker Assignment

Speaker labels describe clusters within one input video; they do not identify
real people. Cluster identifiers are normalized into first-appearance order:
`speaker-1`, `speaker-2`, and so on.

Words that overlap more than one diarization interval are assigned using their
midpoint. Words outside every detected interval receive `speaker-unknown`.
Adjacent words assigned to the same speaker are combined into a block until a
sentence boundary, a meaningful silence, or a maximum block duration makes a
new block more readable.

When the number of speakers is known, `--speakers N` configures clustering with
that count. Otherwise, sherpa-onnx clustering uses its configured similarity
threshold.

Overlapping speech is inherently ambiguous in a single mixed audio track. The
first version assigns each transcribed word to one best matching speaker and
does not claim to separate simultaneous voices into independent transcripts.

## Failure Behavior

These conditions are fatal and return a nonzero status:

- The input is missing, unreadable, or not a regular file.
- FFmpeg or a required model is unavailable.
- The input has no usable audio stream.
- Transcription fails or produces invalid timing data.
- The output parent is unwritable.
- The selected output exists without `--force`.
- A required report artifact cannot be written or validated.

Diarization failure is recoverable. The report is generated with
`speaker-unknown` and a warning records that speaker clustering was unavailable.

Failure to extract one thumbnail is recoverable. The report uses an inline
placeholder for that block and reports the failed timestamp. Widespread
thumbnail failure does not invalidate an otherwise complete transcript.

Temporary audio and staging files are removed on normal completion and handled
with scoped cleanup on errors. A failed run does not leave a directory that
looks like a completed report.

External commands are started with an argument vector rather than a shell
command string. User-provided filenames are never interpreted as shell syntax.

## Components

- `src/cli.*`: parse arguments, derive safe defaults, and validate options.
- `src/process.*`: execute FFmpeg with argument vectors and capture status.
- `src/audio.*`: extract and manage the temporary transcription WAV.
- `src/transcriber.*`: adapt whisper.cpp output into timestamped words.
- `src/diarizer.*`: adapt sherpa-onnx output into speaker intervals.
- `src/segment_merger.*`: assign speakers and build display blocks.
- `src/thumbnails.*`: capture representative video frames.
- `src/report.*`: emit escaped HTML, JSON, and plain text.
- `src/main.cpp`: coordinate stages, progress, warnings, and exit status.
- `tests/`: unit and integration coverage.
- `scripts/download-models.sh`: download free default models and verify
  checksums.
- `CMakeLists.txt`: configure the Linux C++20 build with pinned dependency
  versions.

## Testing

Fast automated tests cover:

- Default output naming for ordinary, dotted, Unicode, and hidden filenames.
- Existing-output collision protection, the report safety marker, and explicit
  `--force`.
- CLI validation and help output.
- Timestamp formatting and boundary behavior.
- Speaker assignment by time overlap and first-appearance label ordering.
- Words outside diarization intervals.
- Block construction across punctuation, silence, and speaker changes.
- UTF-8 preservation plus HTML and JSON escaping.
- Safe process argument handling for spaces and shell metacharacters.
- Missing input, missing audio, missing model, and unwritable output failures.
- Recoverable diarization and individual-thumbnail failures.
- Report completeness, relative paths, and absence of network resources.

An integration test creates a small MP4 fixture with FFmpeg and exercises media
extraction and report generation using deterministic transcription and
diarization test adapters.

A separate opt-in end-to-end smoke test downloads and loads the real free models
because those artifacts are too large for the fast test suite. It verifies the
complete local inference path without asserting exact model prose or cluster
boundaries.

## Initial Scope

The first release targets Linux on x86-64 with CPU inference. Portable CMake
boundaries should not prevent future GPU acceleration or additional operating
systems, but those configurations are outside initial verification.

The first release processes one video per invocation. Batch scheduling, a
desktop GUI, live media, named-person recognition, cloud APIs, and source
separation for overlapping speech are outside scope.
