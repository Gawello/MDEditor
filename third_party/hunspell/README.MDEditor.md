# Hunspell (vendored)

Spell checking. Upstream https://github.com/hunspell/hunspell, tag `v1.7.2`, the library sources of
`src/hunspell/` unchanged. Tri-licensed MPL 1.1 / GPL 2.0 / LGPL 2.1 (`license.hunspell`, `license.myspell`,
`COPYING.MPL`); MDEditor uses it under the MPL 1.1. Built as the static library `hunspell` by
`third_party/CMakeLists.txt`.

Dictionaries are not stored here: `cmake/Dictionaries.cmake` downloads pl_PL and en_US from the LibreOffice
dictionaries repository at configure time (pinned commit, SHA-256 checked) and installs them with the app.
