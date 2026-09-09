cmake_minimum_required(VERSION 3.20)
project(teez-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

include(cmake/TeezResolveCore.cmake)
include(cmake/Dependencies.cmake)
include(cmake/Coverage.cmake)
include(cmake/Tuinator.cmake)
include(cmake/TeezResolveWorker.cmake)

include(${TEEZ_CORE_SOURCE_DIR}/cmake/TeezEmbedWorker.cmake)

add_executable(teez
    src/list_format.cpp
    src/main.cpp
    src/parser.cpp
    src/project_commands.cpp
    src/run_output.cpp
    src/run_output_ui_model.cpp
)

target_include_directories(teez
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(teez
    PRIVATE
        teez_core
        CLI11::CLI11
)

teez_embed_worker(teez)

teez_apply_ui_target(teez)

# --- tests ---
add_executable(teez_cli_tests
    src/list_format.cpp
    src/parser.cpp
    src/project_commands.cpp
    src/run_output.cpp
    src/run_output_ui_model.cpp
    tests/test_list_format.cpp
    tests/test_main_integration.cpp
    tests/test_parser.cpp
    tests/test_project_commands.cpp
    tests/test_run_output.cpp
    tests/test_run_output_branches.cpp
    tests/test_run_output_ui.cpp
    tests/test_run_output_ui_negative.cpp
    tests/test_run_output_ui_model.cpp
)

target_include_directories(teez_cli_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(teez_cli_tests
    PRIVATE
        teez_core
        CLI11::CLI11
        Catch2::Catch2WithMain
)

target_compile_definitions(teez_cli_tests
    PRIVATE
        TEEZ_CLI_BIN="$<TARGET_FILE:teez>"
        TEEZ_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_dependencies(teez_cli_tests teez)

teez_apply_ui_target(teez_cli_tests)

catch_discover_tests(teez_cli_tests)
teez_apply_gcov_prebuild_cleanup()
teez_add_coverage_target(coverage "teez-cli")

include(${TEEZ_CORE_SOURCE_DIR}/cmake/Fuzz.cmake)

teez_add_fuzz_target(fuzz_teez
    CORPUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus"
    SOURCES
        fuzz/fuzz_teez.cpp
        src/list_format.cpp
        src/parser.cpp
        src/run_output.cpp
        src/run_output_ui.cpp
        src/run_output_ui_model.cpp
    LINK_LIBRARIES
        teez_core
        CLI11::CLI11
    COMPILE_DEFINITIONS
        TEEZ_FUZZ_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

if(TARGET fuzz_teez)
    target_include_directories(fuzz_teez
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${TEEZ_CORE_SOURCE_DIR}/fuzz
    )

    teez_embed_worker(fuzz_teez)
    if(TEEZ_HAS_EMBEDDED_WORKER)
        target_compile_definitions(fuzz_teez PRIVATE TEEZ_FUZZ_WORKER=1)
    endif()
endif()
