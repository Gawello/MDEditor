# MDEditor

A lightweight, cross-platform Markdown editor with **live preview in a single pane**, inspired by Obsidian.
No split view and no vault: open any `.md` file from anywhere on disk and the text formats itself as you type.
The line under the cursor shows raw Markdown; every other line is rendered.

Written in C++20 with Qt 6 Widgets. Runs on Windows and Linux.

## Features

- Live preview: headings, **bold**, *italic*, ~~strikethrough~~, ==highlight==, `inline code`, fenced code blocks,
  quotes, bullet/ordered lists, task lists with clickable checkboxes, horizontal rules
- Links: `[text](url)`, bare URLs and `[[wiki links]]`. Ctrl+click opens them (relative links resolve next to the current file)
- Smart lists: Enter continues the list, Enter on an empty item ends it, Tab / Shift+Tab indent
- Formatting shortcuts: Ctrl+B, Ctrl+I, Ctrl+\`, Ctrl+Shift+X (strike), Ctrl+Shift+H (highlight)
- Optional folder panel (Ctrl+Shift+E) to browse any folder, without indexing anything
- Files are saved exactly as typed: UTF-8 BOM and CRLF line endings are preserved, writes are atomic
- Follows the system light/dark palette

## Building

Requirements: CMake ≥ 3.21, a C++20 compiler, Ninja, and Qt ≥ 6.5 (Widgets).

- **Windows:** install Qt 6.8 LTS with the [Qt Online Installer](https://www.qt.io/download-qt-installer)
  (MSVC 2022 64-bit or MinGW), then point CMake at it, e.g. `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`
  (in CLion: *Settings → Build → CMake → CMake options*).
- **Debian/Ubuntu:** `sudo apt install qt6-base-dev cmake ninja-build g++`
- **Fedora:** `sudo dnf install qt6-qtbase-devel cmake ninja-build gcc-c++`

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release --prefix dist   # bundles the Qt runtime next to the executable
```

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

Tabs and recent files, tables, inline images, syntax highlighting in code blocks, search & replace,
HTML/PDF export, AppImage and installer packages, translations.

## License

[MIT](LICENSE)
