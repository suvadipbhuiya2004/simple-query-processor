cmake_minimum_required(VERSION 3.16)
project(QueryEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Google Test setup ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Main target ---
add_library(query_engine_lib
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

target_include_directories(query_engine_lib PUBLIC include)

add_executable(query_engine main.cpp)
target_link_libraries(query_engine PRIVATE query_engine_lib)

if(MSVC)
    target_compile_options(query_engine_lib PRIVATE /W4 /permissive-)
else()
    target_compile_options(query_engine_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Tests ---
enable_testing()
add_subdirectory(tests)