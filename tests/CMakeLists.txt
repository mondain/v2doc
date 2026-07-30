include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG b8acae26b0453dad6250447ad32f6b0015b162ae
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(v2doc_cli_tests test_cli.cpp)
target_link_libraries(v2doc_cli_tests PRIVATE
    v2doc_core
    Catch2::Catch2WithMain
)
target_compile_options(v2doc_cli_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

add_test(NAME cli COMMAND v2doc_cli_tests)

add_executable(v2doc_process_tests test_process.cpp)
target_link_libraries(v2doc_process_tests PRIVATE
    v2doc_core
    Catch2::Catch2WithMain
)
target_compile_options(v2doc_process_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
add_test(NAME process COMMAND v2doc_process_tests)

add_executable(v2doc_audio_tests test_audio.cpp)
target_link_libraries(v2doc_audio_tests PRIVATE
    v2doc_core
    Catch2::Catch2WithMain
)
target_compile_options(v2doc_audio_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
add_test(NAME audio COMMAND v2doc_audio_tests)

add_executable(v2doc_segment_merger_tests test_segment_merger.cpp)
target_link_libraries(v2doc_segment_merger_tests PRIVATE
    v2doc_core
    Catch2::Catch2WithMain
)
target_compile_options(v2doc_segment_merger_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
add_test(NAME segment_merger COMMAND v2doc_segment_merger_tests)

add_executable(v2doc_report_tests test_report.cpp)
target_link_libraries(v2doc_report_tests PRIVATE
    v2doc_core
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
)
target_compile_options(v2doc_report_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
add_test(NAME report COMMAND v2doc_report_tests)

add_executable(v2doc_output_transaction_tests test_output_transaction.cpp)
target_link_libraries(v2doc_output_transaction_tests PRIVATE
    v2doc_core
    Catch2::Catch2WithMain
)
target_compile_options(v2doc_output_transaction_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
add_test(NAME output_transaction COMMAND v2doc_output_transaction_tests)
