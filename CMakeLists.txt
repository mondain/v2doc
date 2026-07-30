cmake_minimum_required(VERSION 3.24)

project(v2doc VERSION 0.1.0 LANGUAGES CXX)

option(V2DOC_ENABLE_INFERENCE "Build local inference adapters and CLI" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(v2doc_core
    src/audio.cpp
    src/cli.cpp
    src/process.cpp
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

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
