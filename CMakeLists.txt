cmake_minimum_required(VERSION 3.24)

project(v2doc VERSION 0.1.0 LANGUAGES CXX)

option(V2DOC_ENABLE_INFERENCE "Build local inference adapters and CLI" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)
set(V2DOC_BUILD_TESTING "${BUILD_TESTING}")

if(NOT V2DOC_ENABLE_INFERENCE)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(v2doc_core STATIC
    src/audio.cpp
    src/cli.cpp
    src/inference_text.cpp
    src/output_transaction.cpp
    src/pipeline.cpp
    src/process.cpp
    src/report.cpp
    src/segment_merger.cpp
    src/thumbnailer.cpp
)
target_include_directories(v2doc_core PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)
target_compile_options(v2doc_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
if(V2DOC_ENABLE_INFERENCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(V2DOC_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        whisper_cpp
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG f049fff95a089aa9969deb009cdd4892b3e74916
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(whisper_cpp)

    set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_CHECK OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_JNI OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_C_API ON CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_GPU OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION ON CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_LINK_LIBSTDCPP_STATICALLY OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_BUILD_C_API_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        sherpa_onnx
        GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
        GIT_TAG d795ccd164bd2360ffda6c3431b1316e2c898be2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sherpa_onnx)

    add_library(v2doc_inference STATIC
        src/diarizer.cpp
        src/transcriber.cpp
    )
    target_include_directories(v2doc_inference PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_compile_options(v2doc_inference PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    target_link_libraries(v2doc_inference PUBLIC
        v2doc_core
        whisper
        sherpa-onnx-c-api
    )
    add_executable(v2doc src/main.cpp)
    target_compile_options(v2doc PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    target_link_libraries(v2doc PRIVATE v2doc_inference)

    set(BUILD_TESTING "${V2DOC_BUILD_TESTING}" CACHE BOOL "" FORCE)
    if(V2DOC_SAVED_BUILD_SHARED_LIBS STREQUAL "")
        unset(BUILD_SHARED_LIBS CACHE)
    else()
        set(
            BUILD_SHARED_LIBS
            "${V2DOC_SAVED_BUILD_SHARED_LIBS}"
            CACHE BOOL "" FORCE
        )
    endif()
endif()

target_link_libraries(v2doc_core PUBLIC nlohmann_json::nlohmann_json)

if(V2DOC_BUILD_TESTING)
    add_subdirectory(tests)
endif()
