add_executable(unit_tests
    lexer_test.cpp
    parser_test.cpp
    execution_test.cpp
    expression_test.cpp
)

target_link_libraries(unit_tests
    GTest::gtest_main
    query_engine_lib
)

include(GoogleTest)
gtest_discover_tests(unit_tests)
