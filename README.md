# MDEditor

A lightweight, cross-platform Markdown editor with **live preview in a single pane**, inspired by Obsidian.
No split view and no vault: open any `.md` file from anywhere on disk and the text formats itself as you type.
The line under the cursor shows raw Markdown; every other line is rendered.

Written in C++20 with Qt 6 Widgets. Runs on Windows and Linux.

## Features

- Live preview: headings, **bold**, *italic*, ~~strikethrough~~, ==highlight==, `inline code`, fenced code blocks,
  quotes, bullet/ordered lists, task lists with clickable checkboxes, horizontal rules
- Tables rendered as an aligned grid (column alignment, inline formatting in cells); click a cell to edit
  the Markdown, Tab / Shift+Tab move between cells, Enter adds a row. The table button (top right) or
  Format → Insert Table… opens a Word-style grid to insert an empty table
- Images: a line holding only `![alt](path)` or `![[path]]` shows the image, scaled to the text column.
  Set a width with `![[img.png|300]]` or `![alt|300](img.png)`. Paths are relative to the Markdown file.
  Dropping or pasting image files inserts links to them; pasting a screenshot or a copied image saves it
  as `assets/pasted-image-<timestamp>.png` next to the note and links it
- Links: `[text](url)`, bare URLs and `[[wiki links]]`. Ctrl+click opens them (relative links resolve next to the current file)
- Smart lists: Enter continues the list, Enter on an empty item ends it, Tab / Shift+Tab indent
- Formatting shortcuts: Ctrl+B, Ctrl+I, Ctrl+\`, Ctrl+Shift+X (strike), Ctrl+Shift+H (highlight)
- Tabs (Ctrl+N new, Ctrl+W or middle click to close, Ctrl+Tab to switch); open tabs are restored on the next start
- File → Open Recent with the last 10 files
- Optional folder panel (Ctrl+Shift+E) to browse any folder, without indexing anything
- Files are saved exactly as typed: UTF-8 BOM and CRLF line endings are preserved, writes are atomic
- Settings (Ctrl+,): theme, editor font and size, line length, indentation, pasted images folder,
  formatting and plugins
- Themes for the whole app: System, Light, Dark, Obsidian, Solarized Light/Dark, Nord, Dracula, plus your own
  JSON themes ([docs/themes.md](docs/themes.md))
- Format (Shift+Alt+F) and Format Document (Ctrl+Shift+Alt+F), like *prettify* in VS Code: JSON (key order kept),
  XML/HTML, CSS, C/C++/Java/JavaScript/C#/Go (re-indent), Python (indentation), and Markdown table alignment.
  Uses clang-format, ruff/black, prettier or gofmt automatically when installed; optional format on save
- JavaScript plugins: commands, code formatters, events (open / before save / save) and themes, with a
  "Create plugin…" button ([docs/plugins.md](docs/plugins.md), [examples](examples/plugins))

## Building

Requirements: CMake ≥ 3.21, a C++20 compiler, Ninja, and Qt ≥ 6.5 (Widgets and Qml).

- **Windows:** install Qt 6.8 LTS with the [Qt Online Installer](https://www.qt.io/download-qt-installer)
  (MSVC 2022 64-bit or MinGW), then point CMake at it, e.g. `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`
  (in CLion: *Settings → Build → CMake → CMake options*).
- **Debian/Ubuntu:** `sudo apt install qt6-base-dev qt6-declarative-dev cmake ninja-build g++`
- **Fedora:** `sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel cmake ninja-build gcc-c++`

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release --prefix "$PWD/dist"   # bundles the Qt runtime; the prefix must be absolute
```

### Running from CLion (Windows)

Create a `CMakeUserPresets.json` (git-ignored) pointing at your Qt installation:

```json
{
  "version": 3,
  "configurePresets": [
    { "name": "qt-debug", "displayName": "Debug (Qt)", "inherits": "debug",
      "cacheVariables": { "CMAKE_PREFIX_PATH": "C:/Qt/6.8.0/mingw_64" } }
  ]
}
```

Enable the *Debug (Qt)* profile in *Settings → Build, Execution, Deployment → CMake* and run the `MDEditor`
configuration. On Windows the build copies the Qt and MinGW runtime DLLs next to the executable and writes a
`qt.conf`, so the app starts straight from the build directory.

The `core` preset builds only the Markdown parser and its tests, with no Qt needed.

## Project layout

| Path | Contents |
|------|----------|
| `src/markdown/` | Qt-independent line and inline Markdown parser |
| `src/editor/` | `MarkdownEditor` (QPlainTextEdit) and the live-preview highlighter |
| `src/document/` | File loading and saving |
| `src/panels/` | Folder panel |
| `src/app/` | Main window |
| `tests/` | Parser unit tests, `tests/data/sample.md` for manual testing |

## Roadmap

Remote (http) images, syntax highlighting in code blocks, search & replace,
HTML/PDF export, AppImage and installer packages, translations.

## License

[MIT](LICENSE)
