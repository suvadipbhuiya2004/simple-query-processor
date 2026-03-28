cmake_minimum_required(VERSION 3.16)
project(QueryEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(query_engine
    main.cpp
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

target_include_directories(query_engine PRIVATE include)

if(MSVC)
    target_compile_options(query_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(query_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()