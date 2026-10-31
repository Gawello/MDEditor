add_executable(mdcore_tests
    test_main.cpp
    test_block_parser.cpp
    test_inline_parser.cpp
    test_table_parser.cpp
    test_formatters.cpp
)
target_link_libraries(mdcore_tests PRIVATE mdcore)
add_test(NAME mdcore_tests COMMAND mdcore_tests)

# Application-level tests (Qt, run offscreen): plugins and example plugins.
if(TARGET mdeditor_app)
    add_executable(app_tests app_test_main.cpp test_plugins.cpp)
    target_link_libraries(app_tests PRIVATE mdeditor_app)
    target_compile_definitions(app_tests PRIVATE
        MDEDITOR_EXAMPLE_PLUGINS="${PROJECT_SOURCE_DIR}/examples/plugins")
    mdeditor_make_runnable(app_tests NO_QT_CONF) # built next to MDEditor, which writes qt.conf
    add_test(NAME app_tests COMMAND app_tests)
    set_tests_properties(app_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
