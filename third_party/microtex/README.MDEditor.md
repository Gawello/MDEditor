# MicroTeX (vendored)

LaTeX math rendering for `$...$` and `$$...$$` in notes.

- Upstream: https://github.com/NanoMichael/MicroTeX, branch `master`, commit `0e3707f6dafebb121d98b53c64364d16fefe481d`
- License: MIT (`LICENSE`)
- Included: `src/` without the samples, the Cairo/GDI/Skia backends and the meson files; `res/fonts/` (the TeX
  fonts, compiled into the app as Qt resources). The Cyrillic and Greek text fonts are left out: text in
  `\text{...}` falls back to system fonts.
- Local changes, also in `mdeditor.patch`:
  - `src/platform/qt/graphic_qt.cpp`, `Graphics2D_qt::drawText`: TeX glyphs are drawn at their final size instead
    of a ~1pt font enlarged by the painter's scale (FreeType crashed on those in Qt 6.10), and the size is
    converted from pixels to points with the device's DPI (glyphs were dpi/72 times too big and overlapped).

Built by `third_party/CMakeLists.txt` as the static library `microtex`, with tinyxml2 (`../tinyxml2`, zlib
license, version 10.0.0) which MicroTeX uses to read optional XML resources.
