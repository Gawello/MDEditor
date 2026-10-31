# Installers built with CPack from the install tree (which already contains the deployed Qt runtime):
#   Windows: NSIS setup .exe (cpack -G NSIS) and a portable .zip (cpack -G ZIP)
#   Linux:   .deb (cpack -G DEB) installing into /opt/mdeditor with its own Qt; see packaging/linux/deb
# The AppImage is assembled in CI from `cmake --install` (see .github/workflows/build.yml).

set(CPACK_PACKAGE_NAME "MDEditor")
set(CPACK_PACKAGE_VENDOR "MDEditor")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Markdown editor with live preview in a single pane")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Gawello/MDEditor")
set(CPACK_PACKAGE_CONTACT "Gawello <Gawello@users.noreply.github.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "MDEditor")
set(CPACK_PACKAGE_EXECUTABLES "MDEditor" "MDEditor") # Start menu shortcut: bin/MDEditor.exe
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_VERBATIM_VARIABLES ON) # values below are written as-is (NSIS quoting and backslashes)

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_FILE_NAME "MDEditor-${PROJECT_VERSION}-win64")
    set(CPACK_NSIS_PACKAGE_NAME "MDEditor ${PROJECT_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME "MDEditor")
    file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/resources/icons/mdeditor.ico" icon) # NSIS wants backslashes
    set(CPACK_NSIS_MUI_ICON "${icon}")
    set(CPACK_NSIS_MUI_UNIICON "${icon}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\MDEditor.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/Gawello/MDEditor")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "MDEditor.exe")
    set(CPACK_CREATE_DESKTOP_LINKS "MDEditor")
    # Offer MDEditor in "Open with" for .md / .markdown files (without taking over the default app).
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [[
  WriteRegStr HKLM "Software\Classes\MDEditor.Markdown" "" "Markdown document"
  WriteRegStr HKLM "Software\Classes\MDEditor.Markdown\DefaultIcon" "" "$INSTDIR\bin\MDEditor.exe,0"
  WriteRegStr HKLM "Software\Classes\MDEditor.Markdown\shell\open\command" "" '"$INSTDIR\bin\MDEditor.exe" "%1"'
  WriteRegStr HKLM "Software\Classes\.md\OpenWithProgids" "MDEditor.Markdown" ""
  WriteRegStr HKLM "Software\Classes\.markdown\OpenWithProgids" "MDEditor.Markdown" ""
  WriteRegStr HKLM "Software\Classes\Applications\MDEditor.exe\shell\open\command" "" '"$INSTDIR\bin\MDEditor.exe" "%1"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
]])
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [[
  DeleteRegKey HKLM "Software\Classes\MDEditor.Markdown"
  DeleteRegValue HKLM "Software\Classes\.md\OpenWithProgids" "MDEditor.Markdown"
  DeleteRegValue HKLM "Software\Classes\.markdown\OpenWithProgids" "MDEditor.Markdown"
  DeleteRegKey HKLM "Software\Classes\Applications\MDEditor.exe"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
]])
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    # Self-contained: the app and its bundled Qt live in /opt/mdeditor, so it doesn't depend on (or clash
    # with) the distribution's Qt version. The maintainer scripts link the launcher, menu entry and icons.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/mdeditor")
    set(CPACK_DEBIAN_PACKAGE_NAME "mdeditor")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT) # mdeditor_<version>_amd64.deb
    set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
        "Markdown editor with live preview in a single pane, inspired by Obsidian.\nNo vaults: open any Markdown file anywhere on disk.")
    # System libraries the bundled Qt (xcb platform plugin) needs; t64 names for Ubuntu 24.04+.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libgl1, libegl1, libfontconfig1, libfreetype6, libdbus-1-3, libglib2.0-0t64 | libglib2.0-0, libxkbcommon0, libxkbcommon-x11-0, libx11-xcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, libxcb-xinerama0, libxcb-xkb1")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${PROJECT_SOURCE_DIR}/packaging/linux/deb/postinst"
        "${PROJECT_SOURCE_DIR}/packaging/linux/deb/prerm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
endif()

include(CPack)
