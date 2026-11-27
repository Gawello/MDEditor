# Spell-checking dictionaries bundled with the app: Polish and US English from the LibreOffice dictionaries
# repository (pinned commit, checked by hash). Without network access the build goes on without them; the app
# then uses the system's dictionaries (Linux) or ones the user adds.
option(MDEDITOR_BUNDLE_DICTIONARIES "Download and install the pl_PL and en_US Hunspell dictionaries" ON)

set(mdeditor_dictionary_dir "${CMAKE_BINARY_DIR}/dictionaries")
set(mdeditor_dictionary_files "")

if(MDEDITOR_BUNDLE_DICTIONARIES)
    set(base "https://raw.githubusercontent.com/LibreOffice/dictionaries/79195b017e6d70151337856c59e7d13d311d6630")
    set(entries
        "pl_PL/pl_PL.dic|c0848440599eb88e5aca500418d5f389e562ec2c157b63dbe39d354658ffba49"
        "pl_PL/pl_PL.aff|82973651651aa930335c865b339b98db376ca3dbf3a661b70b9eeb71fdf41dca"
        "pl_PL/README_pl.txt|ce3ad7ab1d3a8b767b8f7dcc870796fbda76bc7ad8cde22f6312b0cf86a5bd11"
        "en/en_US.dic|f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647"
        "en/en_US.aff|e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e"
        "en/README_en_US.txt|168b4c01cb841f766a72f310b065c04d09cff46376c37a81d799593ce751c371")
    file(MAKE_DIRECTORY "${mdeditor_dictionary_dir}")
    foreach(entry IN LISTS entries)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 path)
        list(GET parts 1 hash)
        get_filename_component(name "${path}" NAME)
        set(target "${mdeditor_dictionary_dir}/${name}")
        set(have "")
        if(EXISTS "${target}")
            file(SHA256 "${target}" have)
        endif()
        if(NOT have STREQUAL hash)
            message(STATUS "Downloading dictionary file ${name}")
            file(DOWNLOAD "${base}/${path}" "${target}" STATUS status TIMEOUT 120)
            list(GET status 0 code)
            set(have "")
            if(code EQUAL 0)
                file(SHA256 "${target}" have)
            endif()
            if(NOT have STREQUAL hash)
                file(REMOVE "${target}")
                message(WARNING "Could not download ${name}; building without bundled dictionaries")
                set(mdeditor_dictionary_files "")
                break()
            endif()
        endif()
        list(APPEND mdeditor_dictionary_files "${target}")
    endforeach()
endif()

# Next to the executable in the build tree, so a build (or the tests) runs with them.
function(mdeditor_copy_dictionaries target)
    if(mdeditor_dictionary_files)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/dictionaries"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${mdeditor_dictionary_files} "$<TARGET_FILE_DIR:${target}>/dictionaries")
    endif()
endfunction()

# Also installed with the app.
function(mdeditor_add_dictionaries target)
    if(NOT mdeditor_dictionary_files)
        return()
    endif()
    mdeditor_copy_dictionaries(${target})
    if(WIN32)
        install(FILES ${mdeditor_dictionary_files} DESTINATION ${CMAKE_INSTALL_BINDIR}/dictionaries)
    else()
        install(FILES ${mdeditor_dictionary_files} DESTINATION ${CMAKE_INSTALL_DATADIR}/mdeditor/dictionaries)
    endif()
endfunction()
