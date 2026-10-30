add_executable(mdcore_tests
    test_main.cpp
    test_block_parser.cpp
    test_inline_parser.cpp
)
target_link_libraries(mdcore_tests PRIVATE mdcore)
add_test(NAME mdcore_tests COMMAND mdcore_tests)
