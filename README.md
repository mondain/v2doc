# v2doc

`v2doc` is a Linux command-line application that turns a video file into a
portable transcript report. It:

- transcribes speech locally with multilingual Whisper;
- records start and end timestamps;
- attempts to group speech under anonymous labels such as `speaker-1`;
- captures a video thumbnail for each transcript block; and
- writes HTML, plain-text, and JSON versions of the transcript.

The application processes existing video files such as MP4 files. It does not
handle live streams and has no MoQ integration. It uses free, local models and
does not call paid transcription services. An Internet connection is needed
only while fetching build dependencies and downloading the models.

## Requirements

The first release targets 64-bit Linux with CPU inference. Building and running
it requires:

- CMake 3.24 or newer;
- a C++20 compiler and build tools;
- Git, used by CMake to fetch pinned source dependencies;
- FFmpeg, available as the `ffmpeg` command; and
- `curl`, `sha256sum`, `tar`, and bzip2 support for the model downloader.

On Debian or Ubuntu, install the general prerequisites with:

```bash
sudo apt update
sudo apt install build-essential git ffmpeg curl coreutils tar bzip2
```

Check `cmake --version` separately. Some older distribution releases provide a
CMake version below the required 3.24; use a current distribution package or
backport if necessary.

The first configure downloads pinned whisper.cpp, sherpa-onnx, ONNX Runtime,
and JSON library sources. The model setup downloads approximately 535 MB.
Allow additional disk space and build time for the compiled dependencies.

## Build

From the repository root:

```bash
cmake -S . -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DV2DOC_ENABLE_INFERENCE=ON \
  -DBUILD_TESTING=ON
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

The executable is written to `build/release/v2doc`. Keep it in its build tree:
the current build uses shared inference libraries from that tree.

## Download the models

Install the default multilingual transcription and speaker-diarization models:

```bash
./scripts/download-models.sh
```

The script verifies every downloaded asset with SHA-256 before installing it.
By default, models are stored in:

```text
${V2DOC_MODEL_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/v2doc/models}
```

To use another location:

```bash
./scripts/download-models.sh --model-dir /path/to/v2doc-models
```

Set `V2DOC_MODEL_DIR` when running v2doc from that custom location:

```bash
V2DOC_MODEL_DIR=/path/to/v2doc-models \
  ./build/release/v2doc video.mp4
```

Once the dependencies and models are present, processing a video is fully
offline.

## Create a transcript

Pass one video file to the executable:

```bash
./build/release/v2doc /path/to/video.mp4
```

When `--output` is omitted, v2doc derives the report directory from the input
filename and creates it in the current directory:

```text
/path/to/video.mp4 -> ./video-transcript/
```

For example:

```bash
./build/release/v2doc /home/mondain/Videos/Wedding-Toast-Tips.mp4
xdg-open Wedding-Toast-Tips-transcript/index.html
```

Choose an explicit output directory when needed:

```bash
./build/release/v2doc video.mp4 --output /path/to/report
```

Open `index.html` directly in a browser. The report does not require a web
server or network connection.

## Command-line options

```text
Usage: v2doc [options] VIDEO

Options:
  --output DIR                Report directory
  --model FILE                Multilingual whisper.cpp model
  --diarization-models DIR    sherpa-onnx model directory
  --language CODE             Language code (default: auto-detect)
  --speakers N                Known number of speakers
  --thumbnail-width N         Thumbnail width (default: 320)
  --no-diarization            Do not identify speakers
  --force                     Replace an existing v2doc report
  --help                      Show this help
```

Useful examples:

```bash
# Hint that the recording contains exactly three speakers.
./build/release/v2doc interview.mp4 --speakers 3

# Force a known spoken language instead of automatic detection.
./build/release/v2doc speech.mp4 --language fr

# Create a transcript without speaker clustering.
./build/release/v2doc lecture.mp4 --no-diarization

# Replace an earlier report created by v2doc.
./build/release/v2doc interview.mp4 --force
```

`--force` only replaces an empty directory or a directory containing v2doc's
report marker. It refuses to replace an unrelated nonempty directory.

## Generated report

A successful run produces:

```text
video-transcript/
|-- .v2doc-report
|-- index.html
|-- transcript.json
|-- transcript.txt
`-- thumbnails/
    |-- segment-0001.jpg
    |-- segment-0002.jpg
    `-- ...
```

- `index.html` is a responsive, self-contained report that refers only to local
  thumbnails in the report directory.
- `transcript.txt` is a readable timestamped transcript.
- `transcript.json` contains structured UTF-8 transcript blocks, millisecond
  timestamps, speaker labels, text, warnings, and relative thumbnail paths.
- `thumbnails/` contains timestamp-matched JPEG video frames.
- `.v2doc-report` marks the directory as safe for a later `--force` replacement.

Copy the complete report directory when sharing or archiving it. The report
does not copy or embed the source video.

## Languages and speaker labels

The default `ggml-small.bin` Whisper model is multilingual. v2doc detects the
spoken language automatically unless `--language CODE` is supplied.

Speaker diarization groups similar voices within one video. Labels such as
`speaker-1` and `speaker-2` are generated in first-appearance order; they do
not identify real people. Supplying the known count with `--speakers N` can
improve clustering.

Diarization is an estimate. Background noise, short utterances, similar voices,
and overlapping speech can produce incorrect labels. Words outside a detected
speaker interval use `speaker-unknown`. The first release does not separate
simultaneous voices into independent audio tracks or transcripts.

## Troubleshooting

### `FFmpeg is unavailable`

Install FFmpeg and verify:

```bash
ffmpeg -version
```

### `Whisper model is not a regular file`

Run `./scripts/download-models.sh`, or select an existing multilingual
whisper.cpp model:

```bash
./build/release/v2doc video.mp4 --model /path/to/ggml-model.bin
```

### `diarization model files are missing`

Run the model downloader again. With a custom model directory, pass the same
root through `V2DOC_MODEL_DIR` or `--diarization-models`.

To proceed without speaker grouping:

```bash
./build/release/v2doc video.mp4 --no-diarization
```

### `output directory already exists`

Choose a different `--output` directory, or use `--force` only when replacing
a report previously created by v2doc.

### The input has no usable audio

v2doc requires an audio stream that FFmpeg can decode. Inspect the input with:

```bash
ffprobe /path/to/video.mp4
```

### Processing is slow

The first release performs transcription and diarization on the CPU. Runtime
depends on video duration, processor speed, and the selected Whisper model.
The default small model balances CPU speed and transcription accuracy.
