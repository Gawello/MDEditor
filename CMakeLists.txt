cmake_minimum_required(VERSION 3.21)
project(MDEditor VERSION 0.1.0 DESCRIPTION "Live-preview Markdown editor" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

option(MDEDITOR_BUILD_APP "Build the Qt application (disable to build only the parser and its tests)" ON)
include(CTest)

# Qt-independent Markdown parsing, shared by the app and the tests.
add_library(mdcore STATIC
    src/markdown/BlockParser.cpp
    src/markdown/InlineParser.cpp
)
target_include_directories(mdcore PUBLIC src)

if(MDEDITOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()

    qt_add_executable(MDEditor WIN32
        src/main.cpp
        src/app/MainWindow.cpp src/app/MainWindow.h
        src/document/Document.cpp src/document/Document.h
        src/editor/EditorTheme.cpp src/editor/EditorTheme.h
        src/editor/LivePreviewHighlighter.cpp src/editor/LivePreviewHighlighter.h
        src/editor/MarkdownEditor.cpp src/editor/MarkdownEditor.h
        src/panels/FolderPanel.cpp src/panels/FolderPanel.h
    )
    target_link_libraries(MDEditor PRIVATE mdcore Qt6::Widgets)
    target_compile_definitions(MDEditor PRIVATE MDEDITOR_VERSION="${PROJECT_VERSION}")

    include(GNUInstallDirs)
    install(TARGETS MDEditor
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
    )
    if(UNIX AND NOT APPLE)
        install(FILES packaging/linux/mdeditor.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    endif()

    # `cmake --install` copies the Qt runtime next to the executable (windeployqt on Windows).
    qt_generate_deploy_app_script(
        TARGET MDEditor
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
