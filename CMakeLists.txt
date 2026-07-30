cmake_minimum_required(VERSION 3.24)

project(v2doc VERSION 0.1.0 LANGUAGES CXX)

option(V2DOC_ENABLE_INFERENCE "Build local inference adapters and CLI" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(v2doc_core
    src/audio.cpp
    src/cli.cpp
    src/output_transaction.cpp
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
target_link_libraries(v2doc_core PUBLIC nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
