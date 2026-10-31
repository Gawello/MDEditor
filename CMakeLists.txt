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
    src/format/CodeFormatters.cpp
    src/markdown/BlockParser.cpp
    src/markdown/InlineParser.cpp
    src/markdown/TableParser.cpp
)
target_include_directories(mdcore PUBLIC src)

if(MDEDITOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Qml)
    qt_standard_project_setup()

    # Everything but main(), so tests can link the real application code.
    qt_add_library(mdeditor_app STATIC
        src/app/AppSettings.cpp src/app/AppSettings.h
        src/app/EditorTab.cpp src/app/EditorTab.h
        src/app/FindBar.cpp src/app/FindBar.h
        src/app/SettingsDialog.cpp src/app/SettingsDialog.h
        src/app/MainWindow.cpp src/app/MainWindow.h
        src/app/RecentFiles.cpp src/app/RecentFiles.h
        src/app/TabCloseButton.cpp src/app/TabCloseButton.h
        src/app/TablePicker.cpp src/app/TablePicker.h
        src/document/Document.cpp src/document/Document.h
        src/editor/EditorTheme.cpp src/editor/EditorTheme.h
        src/editor/LivePreviewHighlighter.cpp src/editor/LivePreviewHighlighter.h
        src/editor/MarkdownEditor.cpp src/editor/MarkdownEditor.h
        src/format/FormatterRegistry.cpp src/format/FormatterRegistry.h
        src/panels/FolderPanel.cpp src/panels/FolderPanel.h
        src/plugins/PluginApi.cpp src/plugins/PluginApi.h
        src/plugins/PluginManager.cpp src/plugins/PluginManager.h
        src/theme/Theme.cpp src/theme/Theme.h
        src/theme/ThemeManager.cpp src/theme/ThemeManager.h
    )
    target_link_libraries(mdeditor_app PUBLIC mdcore Qt6::Widgets Qt6::Qml)

    qt_add_executable(MDEditor WIN32 src/main.cpp)
    target_link_libraries(MDEditor PRIVATE mdeditor_app)
    target_compile_definitions(MDEditor PRIVATE MDEDITOR_VERSION="${PROJECT_VERSION}")

    # Make a build-directory executable runnable as is (from an IDE or by double click) on Windows:
    # copy the Qt and compiler runtime DLLs next to it and point Qt at its plugins via qt.conf.
    # NO_QT_CONF: the target shares its output directory with one that already writes qt.conf.
    function(mdeditor_make_runnable target)
        cmake_parse_arguments(PARSE_ARGV 1 arg "NO_QT_CONF" "" "")
        if(NOT WIN32)
            return()
        endif()
        set(runtime_dlls "")
        if(MINGW)
            get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            foreach(dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
                if(EXISTS "${compiler_dir}/${dll}")
                    list(APPEND runtime_dlls "${compiler_dir}/${dll}")
                endif()
            endforeach()
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}> ${runtime_dlls} $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )
        if(NOT arg_NO_QT_CONF)
            file(GENERATE OUTPUT "$<TARGET_FILE_DIR:${target}>/qt.conf"
                CONTENT "[Paths]\nPrefix=${QT6_INSTALL_PREFIX}\n")
        endif()
    endfunction()
    mdeditor_make_runnable(MDEditor)

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
