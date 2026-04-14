cmake_minimum_required(VERSION 3.16)

project(QueryEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(QUERY_ENGINE_SOURCES
    src/app/cli/cli_options.cpp
    src/app/cli/line_editor.cpp
    src/app/cli/query_engine_cli.cpp
    src/app/query_engine_app.cpp
    src/app/sql_script_loader.cpp
    src/common/data_type.cpp
    src/parser/ast.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/planner/plan.cpp
    src/planner/optimizer.cpp
    src/planner/statistics.cpp
    src/planner/cost_model.cpp
    src/storage/table.cpp
    src/storage/csv_loader.cpp
    src/storage/metadata_catalog.cpp
    src/execution/compiled_predicate.cpp
    src/execution/expression.cpp
    src/execution/seq_scan_executor.cpp
    src/execution/filter_executor.cpp
    src/execution/distinct_executor.cpp
    src/execution/projection_executor.cpp
    src/execution/sort_executor.cpp
    src/execution/index_scan_executor.cpp
    src/execution/limit_executor.cpp
    src/execution/executor_builder.cpp
    src/execution/aggregation_executor.cpp
    src/execution/join_executor.cpp
)

add_library(query_engine_lib ${QUERY_ENGINE_SOURCES})
target_compile_features(query_engine_lib PUBLIC cxx_std_17)
target_include_directories(query_engine_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(query_engine_lib PRIVATE /W4 /permissive-)
else()
    target_compile_options(query_engine_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(query_engine main.cpp)
target_link_libraries(query_engine PRIVATE query_engine_lib)

if(BUILD_TESTING)
    include(FetchContent)

    # For Windows: prevent overriding the parent project's compiler/linker settings.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP ON
    )
    FetchContent_MakeAvailable(googletest)

    set(QUERY_ENGINE_TEST_SOURCES
        tests/lexer_test.cpp
        tests/parser_test.cpp
        tests/execution_test.cpp
        tests/expression_test.cpp
        tests/aggregation_edge_cases_test.cpp
        tests/alter_table_test.cpp
        tests/data_type_test.cpp
    )

    add_executable(unit_tests ${QUERY_ENGINE_TEST_SOURCES})
    target_link_libraries(unit_tests PRIVATE GTest::gtest_main query_engine_lib)
    target_include_directories(unit_tests PRIVATE ${googletest_SOURCE_DIR}/googletest/include)

    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()