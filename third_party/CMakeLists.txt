# Third-party code: built with its own warning settings, since it isn't ours to keep warning-free.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
if(MSVC)
    add_compile_options(/utf-8 /W0)
else()
    add_compile_options(-w)
endif()

file(GLOB_RECURSE microtex_sources CONFIGURE_DEPENDS microtex/src/*.cpp)
add_library(microtex STATIC ${microtex_sources} tinyxml2/tinyxml2.cpp)
target_include_directories(microtex SYSTEM PUBLIC microtex/src tinyxml2) # their warnings aren't ours
target_compile_definitions(microtex PUBLIC BUILD_QT)
target_compile_features(microtex PUBLIC cxx_std_17)
target_link_libraries(microtex PUBLIC Qt6::Gui)

# The TeX fonts, loaded from ":/res/fonts/..." (MicroTeX falls back to the resource path).
file(GLOB_RECURSE microtex_fonts RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/microtex microtex/res/fonts/*.ttf)
list(TRANSFORM microtex_fonts PREPEND microtex/)
qt_add_resources(microtex "microtex_fonts" PREFIX "/" BASE microtex FILES ${microtex_fonts})

# Hunspell 1.7.2 (MPL 1.1 / GPL 2 / LGPL 2.1; used under the MPL), for spell checking.
file(GLOB hunspell_sources CONFIGURE_DEPENDS hunspell/*.cxx)
add_library(hunspell STATIC ${hunspell_sources})
target_include_directories(hunspell SYSTEM PUBLIC hunspell)
target_compile_definitions(hunspell PUBLIC HUNSPELL_STATIC)
