# CLAUDE.md

Guidance for Claude Code when working in this repository. **Keep this file up to date**: when a change
affects the build, architecture, conventions or a pitfall listed here, update the relevant section in the
same change.

## Project

MDEditor is an open-source (MIT) Markdown editor inspired by Obsidian, written in C++20 with Qt 6 Widgets,
targeting Windows and Linux. Key product decisions:

- **Single-pane live preview**: no split view. The block (line) under the cursor shows raw Markdown with dimmed
  markers; every other block is rendered in place. Tables switch to raw mode as a whole while the cursor is
  anywhere inside them.
- **No vaults**: any file anywhere on disk can be opened; the folder panel just browses an ad-hoc folder.
- Files are saved byte-faithfully: what the user typed, with BOM / CRLF / non-breaking spaces preserved.

The owner communicates in Polish; replies go in Polish. Code, comments, UI strings (wrapped in `tr()`), commit
messages and docs are in English.

## Build & test

Presets live in `CMakePresets.json` (`debug`, `release`, `core`). `core` builds only the Qt-free parser
library and its tests. A git-ignored `CMakeUserPresets.json` adds `qt-debug` / `qt-release`, which point
`CMAKE_PREFIX_PATH` at the local Qt (`C:/Qt/6.8.0/mingw_64`); CLion picks these up as profiles.

On this Windows machine cmake, ninja and MinGW are not on PATH. Use CLion's bundled tools:

```powershell
$b="$env:LOCALAPPDATA\Programs\CLion\bin"; $env:PATH="$b\mingw\bin;$b\cmake\win\x64\bin;$b\ninja\win\x64;$env:PATH"
cmake --preset qt-debug; cmake --build --preset qt-debug; ctest --preset qt-debug
cmake --build --preset core; ctest --preset core          # parser only, no Qt needed
```

- On Windows a post-build step copies the Qt and MinGW runtime DLLs next to `MDEditor.exe` and writes a
  `qt.conf`, so `build/qt-debug/MDEditor.exe` runs directly.
- `cmake --install` runs Qt's deploy script, which **requires an absolute `--prefix`**. A relative path makes
  CI fail with "Given qt.conf path is not an absolute path".
- CI: `.github/workflows/build.yml` builds, tests and packages on `windows-latest` (MSVC) and `ubuntu-24.04`.
  Keep the code warning-free on GCC/MinGW and MSVC (`/W4`).

### Testing the GUI

- Parser logic: add cases to `tests/test_*.cpp`. The tests use the tiny harness in `tests/TestHarness.h`
  (`TEST_CASE`, `CHECK`), not Qt Test, so they build without Qt.
- Visual checks: launch the app and take window screenshots. Sending keys with SendKeys only works when Windows
  grants the window focus, which fails while the user is busy at the machine.
- For editor behavior that doesn't need real focus, a scratch driver linking the editor sources and run with
  `QT_QPA_PLATFORM=offscreen` works well: send `QKeyEvent`/`QMouseEvent` with `QApplication::sendEvent`, print
  `markdown()`, and save `widget->grab()` images. Offscreen has no fonts, so glyphs render as boxes.
- Don't leave test instances running: a modified document blocks on the "unsaved changes" dialog, and a running
  `MDEditor.exe` blocks relinking.

## Architecture

```
src/markdown/   Qt-free parsing (pure C++20, std::u16string_view, UTF-16 offsets = QString offsets)
  BlockParser   per-line classification + BlockState carried between lines (fences, tables)
  InlineParser  per-character style flags (Marker, Bold, Italic, Code, Link, ...) + link ranges
  TableParser   GFM table row splitting and delimiter-row parsing
src/editor/
  LivePreviewHighlighter  QSyntaxHighlighter applying formats; hides markers outside the active block/table
  MarkdownEditor          QPlainTextEdit: painting of decorations and tables, list/table editing, links
  EditorTheme             colors/fonts derived from the palette (light/dark)
src/document/Document   load/save with encoding details
src/panels/FolderPanel  QFileSystemModel tree (no icons, .md suffix hidden)
src/app/                MainWindow (tabs, menus, session), EditorTab, RecentFiles, TabCloseButton, TablePicker
```

### How live preview works

- The highlighter parses each block with `md::parseLine(text, BlockState::decode(previousBlockState()), nextLine)`
  and stores the result in a `BlockData` (`QTextBlockUserData`) so painting doesn't re-parse.
- Markers are hidden with character formats only; the document text is never changed:
  - `collapsed()`: transparent and 0.1pt, so the marker takes almost no space (inline markers, `#`).
  - `invisible()`: transparent but full width, used when the editor paints something in its place
    (bullets, checkboxes, quote bars, rules, whole table rows).
- `MarkdownEditor::paintEvent` paints code-block backgrounds under the text, then decorations and table grids
  on top. It skips the active block and the table containing the cursor.
- `LivePreviewHighlighter::setActiveBlock` tracks the cursor block plus the active table's block range, and
  re-highlights only the blocks whose rendering changes.
- `BlockState` must round-trip through one `int` (`QTextBlock::userState`): fences encode as even values >= 12,
  table states as 1 and 3, the default as -1.
- A table header depends on the *next* line (it needs a delimiter row below it). `recheckPreviousBlock` detects
  when an edit changes the previous line's classification and re-highlights it on a zero-delay timer.

## Pitfalls already hit

- `QTextDocument::setDocumentMargin()` marks the document as modified: reset with `setModified(false)` after setup.
- `setFont()` in a widget constructor triggers `changeEvent(FontChange)` before members exist: guard handlers.
- `QPlainTextEdit` consumes Ctrl+Tab. `MarkdownEditor::event` ignores that `ShortcutOverride` so the window's
  tab-switching shortcut works.
- Use `document()->toRawText()` (not `toPlainText()`, which turns NBSP into spaces) when saving.
- Wheel events over the viewport margins (centered text column) don't reach the viewport; `MarkdownEditor::event`
  forwards them.
- `QSyntaxHighlighter` format changes emit `contentsChange` too. Don't drive re-highlighting from that signal
  (it can loop); use the highlighter itself (see `recheckPreviousBlock`).

## Conventions

- Match the surrounding style: 4-space indent, `m_` members, braces on the next line for functions, short
  comments explaining *why*. Keep `src/markdown` free of Qt.
- UI colors come from the palette via `EditorTheme::mix`; no hard-coded light-only colors.
- Commit only when asked. Commit messages end with the `Co-Authored-By` line given in the session.
