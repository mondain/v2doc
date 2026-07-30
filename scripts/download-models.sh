#!/usr/bin/env bash

set -euo pipefail

readonly WHISPER_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin"
readonly WHISPER_SHA256="1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b"
readonly SEGMENTATION_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-segmentation-models/sherpa-onnx-pyannote-segmentation-3-0.tar.bz2"
readonly SEGMENTATION_SHA256="24615ee884c897d9d2ba09bb4d30da6bb1b15e685065962db5b02e76e4996488"
readonly SEGMENTATION_MODEL_SHA256="220ad67ca923bef2fa91f2390c786097bf305bceb5e261d4af67b38e938e1079"
readonly EMBEDDING_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx"
readonly EMBEDDING_SHA256="1a331345f04805badbb495c775a6ddffcdd1a732567d5ec8b3d5749e3c7a5e4b"
readonly SEGMENTATION_NAME="sherpa-onnx-pyannote-segmentation-3-0"
readonly EMBEDDING_NAME="3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx"

usage() {
    local default_location
    default_location='${V2DOC_MODEL_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/v2doc/models}'
    printf '%s\n' \
        "Usage: download-models.sh [--model-dir DIR]" \
        "" \
        "Download the free multilingual transcription and speaker models used" \
        "by v2doc. Downloads are about 535 MB and are SHA-256 verified before" \
        "installation. Once installed, video processing remains fully offline." \
        "" \
        "Default model location:" \
        "  ${default_location}"
}

model_dir=""
while (($# > 0)); do
    case "$1" in
        --model-dir)
            if (($# < 2)) || [[ -z "$2" ]]; then
                printf '%s\n' "download-models.sh: --model-dir requires a directory" >&2
                exit 2
            fi
            model_dir="$2"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            printf '%s\n' "download-models.sh: unknown option: $1" >&2
            exit 2
            ;;
    esac
done

if [[ -z "$model_dir" ]]; then
    if [[ -n "${V2DOC_MODEL_DIR:-}" ]]; then
        model_dir="$V2DOC_MODEL_DIR"
    elif [[ -n "${XDG_DATA_HOME:-}" ]]; then
        model_dir="$XDG_DATA_HOME/v2doc/models"
    elif [[ -n "${HOME:-}" ]]; then
        model_dir="$HOME/.local/share/v2doc/models"
    else
        printf '%s\n' \
            "download-models.sh: set --model-dir when HOME is unavailable" >&2
        exit 2
    fi
fi

for command_name in curl sha256sum tar mktemp; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf '%s\n' "download-models.sh: required command not found: $command_name" >&2
        exit 1
    fi
done

mkdir -p "$model_dir"
work_dir="$(mktemp -d "$model_dir/.v2doc-model-download.XXXXXX")"
cleanup() {
    rm -rf -- "$work_dir"
}
trap cleanup EXIT

verify_checksum() {
    local path="$1"
    local expected="$2"
    local actual
    actual="$(sha256sum -- "$path")"
    [[ "${actual%% *}" == "$expected" ]]
}

install_file() {
    local label="$1"
    local url="$2"
    local expected="$3"
    local destination="$4"

    if [[ -e "$destination" ]]; then
        if [[ -f "$destination" ]] && verify_checksum "$destination" "$expected"; then
            printf '%s\n' "$label already verified: $destination"
            return
        fi
        printf '%s\n' \
            "download-models.sh: refusing to replace mismatched asset: $destination" >&2
        exit 1
    fi

    local temporary="$work_dir/$(basename "$destination").download"
    printf '%s\n' "Downloading $label..."
    curl --fail --location --retry 3 --output "$temporary" "$url"
    if ! verify_checksum "$temporary" "$expected"; then
        printf '%s\n' "download-models.sh: checksum failed for $label" >&2
        exit 1
    fi
    mv "$temporary" "$destination"
    printf '%s\n' "Installed $label: $destination"
}

printf '%s\n' "Model directory: $model_dir" "Expected download: about 535 MB"

install_file \
    "Whisper multilingual small model" \
    "$WHISPER_URL" \
    "$WHISPER_SHA256" \
    "$model_dir/ggml-small.bin"

segmentation_dir="$model_dir/$SEGMENTATION_NAME"
if [[ -e "$segmentation_dir" ]]; then
    if [[ -f "$segmentation_dir/model.onnx" ]] &&
       verify_checksum \
           "$segmentation_dir/model.onnx" \
           "$SEGMENTATION_MODEL_SHA256"; then
        printf '%s\n' "Speaker segmentation model already verified: $segmentation_dir"
    else
        printf '%s\n' \
            "download-models.sh: refusing to replace mismatched asset: $segmentation_dir" >&2
        exit 1
    fi
else
    segmentation_archive="$work_dir/$SEGMENTATION_NAME.tar.bz2"
    printf '%s\n' "Downloading speaker segmentation model..."
    curl \
        --fail \
        --location \
        --retry 3 \
        --output "$segmentation_archive" \
        "$SEGMENTATION_URL"
    if ! verify_checksum "$segmentation_archive" "$SEGMENTATION_SHA256"; then
        printf '%s\n' \
            "download-models.sh: checksum failed for speaker segmentation model" >&2
        exit 1
    fi
    extraction_dir="$work_dir/extracted"
    mkdir "$extraction_dir"
    tar -xjf "$segmentation_archive" -C "$extraction_dir"
    extracted_model="$extraction_dir/$SEGMENTATION_NAME"
    if [[ ! -f "$extracted_model/model.onnx" ]]; then
        printf '%s\n' \
            "download-models.sh: segmentation archive has an unexpected layout" >&2
        exit 1
    fi
    if ! verify_checksum \
        "$extracted_model/model.onnx" \
        "$SEGMENTATION_MODEL_SHA256"; then
        printf '%s\n' \
            "download-models.sh: extracted segmentation model checksum failed" >&2
        exit 1
    fi
    mv "$extracted_model" "$segmentation_dir"
    printf '%s\n' "Installed speaker segmentation model: $segmentation_dir"
fi

install_file \
    "Speaker embedding model" \
    "$EMBEDDING_URL" \
    "$EMBEDDING_SHA256" \
    "$model_dir/$EMBEDDING_NAME"

printf '%s\n' "All v2doc models are installed and verified."
