cmake_minimum_required(VERSION 3.16)

project(QueryEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

set(QUERY_ENGINE_SOURCES
    src/parser/ast.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/planner/plan_node.cpp
    src/planner/planner.cpp
    src/storage/table.cpp
    src/storage/csv_loader.cpp
    src/execution/expression.cpp
    src/execution/seq_scan_executor.cpp
    src/execution/filter_executor.cpp
    src/execution/projection_executor.cpp
    src/execution/executor_builder.cpp
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
    )

    add_executable(unit_tests ${QUERY_ENGINE_TEST_SOURCES})
    target_link_libraries(unit_tests PRIVATE GTest::gtest_main query_engine_lib)

    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()