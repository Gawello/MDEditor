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

Presets live in `CMakePresets.json` (`debug`, `release`, `core`). `core` builds only the Qt-free `mdcore`
library (parsers + formatters) and its tests. With Qt, all app code except `main.cpp` is the static library
`mdeditor_app`, linked by `MDEditor` and by `app_tests` (Qt tests run offscreen: plugins, examples). A git-ignored `CMakeUserPresets.json` adds `qt-debug` / `qt-release`, which point
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
- Packaging (`cmake/Packaging.cmake`, CPack): NSIS `.exe` + `.zip` on Windows, `.deb` on Linux (self-contained
  in `/opt/mdeditor`; `packaging/linux/deb/postinst|prerm` link `/usr/bin/mdeditor`, the menu entry and
  icons). The AppImage is assembled in CI from `cmake --install` + `packaging/linux/AppRun` with appimagetool.
  Pushing a tag `v<version>` (must equal `project(... VERSION)`) publishes a GitHub Release with all packages.
  Local check: `cpack --config build/qt-debug/CPackConfig.cmake -G ZIP -B <dir>`.
- Icon: `resources/icons/mdeditor.svg` is the source; the PNGs and `mdeditor.ico` (multi-size, PNG entries)
  are rendered from it with a QSvgRenderer-based helper. Re-render them all if the SVG changes.

### Testing the GUI

- Parser/formatter logic: add cases to `tests/test_*.cpp`. The tests use the tiny harness in
  `tests/TestHarness.h` (`TEST_CASE`, `CHECK`), not Qt Test, so they build without Qt.
- App-level tests (`tests/test_plugins.cpp`, `app_tests`) use the same harness with a QApplication and their
  own QSettings organization (`MDEditorTests`), so they never touch the user's settings.
- Visual checks: render widgets off screen (`Qt::WA_DontShowOnScreen` + `grab()`) in a scratch driver. Do **not**
  take desktop screenshots or send any synthetic input (SendKeys, focus changes) on the user's machine: keys go
  to whatever window the user has active (this once opened the browser's history with Ctrl+H and typed into
  it), and screen captures can include the user's other windows.
- To check that a packaged build starts, launch it minimized and list its visible windows passively
  (EnumWindows by process id), then stop the process.
- For editor behavior that doesn't need real focus, a scratch driver linking the editor sources and run with
  `QT_QPA_PLATFORM=offscreen` works well: send `QKeyEvent`/`QMouseEvent` with `QApplication::sendEvent`, print
  `markdown()`, and save `widget->grab()` images. Offscreen has no fonts, so glyphs render as boxes.
  Synthesized drops need a `QDragEnterEvent` (and move) sent to `viewport()` first, or the drop is ignored.
- Don't leave test instances running: a modified document blocks on the "unsaved changes" dialog, and a running
  `MDEditor.exe` blocks relinking.

## Architecture

```
src/markdown/   Qt-free parsing (pure C++20, std::u16string_view, UTF-16 offsets = QString offsets)
  BlockParser   per-line classification + BlockState carried between lines (fences, tables)
  InlineParser  per-character style flags (Marker, Bold, Italic, Code, Link, ...) + link and entity ranges
  Entities      HTML character references; EntityTable.inc and EntityNames.inc (the reverse, best-known name per
                text) are generated by tools/generate_entities.py
  TableParser   GFM table row splitting and delimiter-row parsing
src/editor/
  LivePreviewHighlighter  QSyntaxHighlighter applying formats; hides markers outside the active block/table
  MarkdownEditor          QPlainTextEdit: painting of decorations and tables, list/table editing, links
  EditorTheme             colors/fonts derived from the palette (light/dark)
src/format/
  CodeFormatters          Qt-free: JSON (keeps key order), CSS, C-like re-indent, Python indent, table alignment
  FormatterRegistry       language aliases; plugin formatter > external tool (QProcess) > built-in; XML via Qt
src/theme/              Theme (palette + Markdown colors, JSON), ThemeManager (presets, user/plugin themes)
src/plugins/            PluginManager (discovery, QJSEngine per plugin, 3 s watchdog), PluginApi (`app`, `editor`)
src/document/Document   read/write/check with encoding details (static, off the GUI thread), FileStamp
src/panels/             Sidebar (collapsible sections in one view): FavoritesPanel, and FolderPanel twice, for the
                        vault and for browsing (QFileSystemModel tree, no icons, .md suffix hidden)
src/app/                MainWindow (tabs, menus, status bar, session), EditorTab (editor + FindBar),
                        FindBar (find/replace, highlights via extraSelections), AppSettings, SettingsDialog,
                        Background (runInBackground, runAndWait), ExternalChangeBar, CloudDrive (rclone),
                        CloudDrivePage,
                        RecentFiles, TabCloseButton, TablePicker, SymbolPicker + SymbolSets
docs/                   plugins.md (plugin API), themes.md (theme JSON); examples/plugins/ = sample plugins
```

### Settings, themes, formatting, plugins

- `AppSettings` (singleton over QSettings) emits `changed(key)`; editors and the theme manager react live.
  Keys are constants on the class (`themeKey`, `fontSizeKey`, …, `format/<lang>/command`, `plugins/enabled`).
- `ThemeManager::apply`: non-system themes switch the app to the **Fusion** style + `QApplication::setPalette`
  (native Windows styles ignore custom palettes); System restores the platform style and `setPalette(QPalette())`.
  `EditorTheme::fromPalette` takes accent/code/highlight colors from the current theme.
- Formatting entry points (shown to the user as Prettify, in the Format and right-click menus):
  `MarkdownEditor::formatAtCursor` (selection → fenced block → table → whole non-Markdown file) and `formatDocument`
  (all code blocks + tables, bottom-up, one undo step). Messages go to the status bar via
  `MarkdownEditor::statusMessage`. They keep the cursor in place: table cursors map cell by cell, code cursors by
  non-whitespace characters. Inside `formatDocument` the cursor is set only after the edit block (`m_batchCursor`).
- Plugins: `<AppData>/plugins/<id>/manifest.json + main.js`, disabled until enabled in Settings. Every call into
  JS goes through `PluginManager::guarded` (watchdog thread → `QJSEngine::setInterrupted`). Events are fired from
  `EditorTab::load/save`. Keep `docs/plugins.md` in sync when the API changes.

### Files on slow disks and changes made elsewhere

- Disk access for documents never runs on the GUI thread. `EditorTab::load/save/reload` stay synchronous for
  their callers but wait in `waitWithProgress`: a nested loop that holds back input, then a modal "Stop Waiting"
  dialog after 300 ms. Workers are detached `std::thread`s, not the thread pool: a hung drive never returns, so
  their lambdas capture values only and may outlive the tab.
- `FileStamp` (size, date, SHA-256) records the last version read or written. `Document::check` reads the file
  only if size or date differ. A save that finds a change returns `SaveResult::Conflict` before the format and
  plugin hooks, and `MainWindow::resolveConflict` asks. `EditorTab::checkDisk` runs when the app becomes active:
  unmodified tabs reload, modified ones show `ExternalChangeBar`. There is deliberately no QFileSystemWatcher.
- Still synchronous: images and pasted images in `MarkdownEditor`, and `QFileInfo` calls in `MainWindow`.

### Google Drive

- `CloudDrive` drives rclone with QProcess. rclone owns login and token (`~/.config/rclone/rclone.conf`); AppSettings
  `drive/*` keys hold the folders. Both are in the user's profile, so they survive updates.
- Signed-in means `rclone config dump` has a token for `mdeditor-gdrive`, not `listremotes`: an abandoned login leaves
  a remote without a token (`cancelSignIn` deletes it).
- The mount is detached (`QProcess::startDetached`, no `--daemon`, which Windows lacks) and outlives the app, so a
  pending upload isn't cut off; `start()` finds it still mounted. Mounted means `QStorageInfo` type `fuse.rclone` and
  a readable folder (a killed rclone leaves a dead mount, cleared before reconnecting), probed off the GUI thread. `MainWindow::restoreSession` holds back session items on the drive until `connected`,
  and keeps them for the next session if it never comes.
- `tests/test_cloud_drive.cpp` uses a fake rclone shell script. Google sign-in itself can't be automated. Windows
  paths (`taskkill`, mount folder must not exist) are untested.

### How live preview works

- The highlighter parses each block with `md::parseLine(text, BlockState::decode(previousBlockState()), nextLine)`
  and stores the result in a `BlockData` (`QTextBlockUserData`) so painting doesn't re-parse.
- Markers are hidden with character formats only; the document text is never changed:
  - `collapsed()`: transparent and 0.1pt, so the marker takes almost no space (inline markers, `#`).
  - `invisible()`: transparent but full width, used when the editor paints something in its place
    (bullets, checkboxes, quote bars, rules, whole table rows).
- Character references (`&rarr;`) can't be replaced either: `LivePreviewHighlighter::showEntity` turns the `&` into
  an invisible slot as wide as the character (letter spacing) and collapses the rest; `paintEntities` paints it on top.
  Table cells are painted from `cellRuns`, which substitutes the text directly. Prettify keeps counting the raw text,
  since that's what lines up in the file.
- `MarkdownEditor::paintEvent` paints code-block backgrounds under the text, then decorations and table grids
  on top. It skips the active block and the table containing the cursor.
- `LivePreviewHighlighter::setActiveBlock` tracks the cursor block plus the active table's block range, and
  re-highlights only the blocks whose rendering changes.
- `BlockState` must round-trip through one `int` (`QTextBlock::userState`): fences encode as even values >= 12,
  table states as 1 and 3, the default as -1.
- Images: when a paragraph line holds nothing but one image link (`![](…)` / `![[…]]`), the highlighter
  collapses the text and gives the first character an invisible, huge point size so the line is as tall as
  the image (`highlightImageLine`, `BlockData::imageShown`); `MarkdownEditor::paintImage` then draws it.
  Sizes come from the editor through `ImageSizeProvider`; images are cached by absolute path and reloaded
  when the file's mtime changes. Width changes re-run highlighting (debounced), since sizes depend on the
  text column width. Remote images are not fetched (no QtNetwork).
- Pasting (`MarkdownEditor::insertFromMimeData`): copied image *files* are linked in place; raw image data
  is saved as `assets/pasted-image-<timestamp>.png` next to the note (requires a saved note, so the editor
  knows its path via `setDocumentPath`). If the clipboard has text too, text wins (Word/Excel put both).
- A table header depends on the *next* line (it needs a delimiter row below it). `recheckPreviousBlock` detects
  when an edit changes the previous line's classification and re-highlights it on a zero-delay timer.

## Pitfalls already hit

- `QTextDocument::setDocumentMargin()` marks the document as modified: reset with `setModified(false)` after setup.
- `setFont()` in a widget constructor triggers `changeEvent(FontChange)` before members exist: guard handlers.
- `QPlainTextEdit` consumes Ctrl+Tab. `MarkdownEditor::event` ignores that `ShortcutOverride` so the window's
  tab-switching shortcut works.
- Set an editor's base directory *before* `setMarkdown()`, or relative image paths fail to resolve during
  the first highlight pass.
- Use `document()->toRawText()` (not `toPlainText()`, which turns NBSP into spaces) when saving.
- Wheel events over the viewport margins (centered text column) don't reach the viewport; `MarkdownEditor::event`
  forwards them.
- `QTextDocument::find(QRegularExpression, …)` ignores the pattern's case option and uses the
  `FindCaseSensitively` flag instead (case-insensitive without it); `FindBar::findFrom` sets the flag.
- To render a widget with real fonts but nothing on screen, set `Qt::WA_DontShowOnScreen`, `show()` it and
  `grab()` (no offscreen platform needed).
- MinGW: CLion's GCC 15 differs from the MinGW 13 that built Qt. windeployqt copies Qt's (older) libstdc++ into
  installs, which makes the app hang at startup in the Windows loader. The CMake install step re-installs the
  building compiler's runtime DLLs after the deploy script (`mingw_runtime_dlls`). Not an issue with MSVC (CI).
- On Windows, Qt `#define`s `main` (to `qMain`): never name a member or variable `main`.
- `qt_standard_project_setup()` puts all executables in one output directory; only one target may generate
  `qt.conf` there (`mdeditor_make_runnable(target NO_QT_CONF)`).
- Plugin `QJSEngine`s must be destroyed before `QApplication` (`PluginManager::shutdown()` in `main`), and a
  `QJSValue` must not outlive its engine (in `PluginManager::Plugin` the engine is declared before the
  `QJSValue` members, so it is destroyed after them).
- `QTextBlock`s in a replaced range are invalid afterwards (`blockNumber()` is wrong): save numbers before `insertText`.
- `QSyntaxHighlighter` format changes emit `contentsChange` too. Don't drive re-highlighting from that signal
  (it can loop); use the highlighter itself (see `recheckPreviousBlock`).
- `collapsed()` (0.1pt) rounds to 0 px, which fontconfig can resolve to a bitmap font such as Noto Color Emoji.
  Without `QFont::NoFontMerging`, each glyph missing there walks all system fonts (17 s on the first table with
  2300 fonts). `*` and `#` hide the bug: the emoji font has them.
- Emoji need a color emoji family in the font's list (`EditorTheme::withEmoji`). Left to fallback, Linux gives empty
  boxes or gray glyphs from a symbol font, though Qt draws the bitmap font fine once it's asked for.
- A `Qt::Popup` on X11 grabs the mouse and keyboard of the whole desktop (so `SymbolPicker`, which stays open, is a
  frameless `Qt::Tool` that never takes the focus, and watches the application's events to close like a drop-down): close it before doing work, and return to
  the event loop first (`TablePicker::choose` + queued `picked`), or a slow call freezes every application.

## Conventions

- Match the surrounding style: 4-space indent, `m_` members, braces on the next line for functions, short
  comments explaining *why*. Keep `src/markdown` free of Qt.
- UI colors come from the palette via `EditorTheme::mix`; no hard-coded light-only colors.
- Commit only when asked. Commit messages end with the `Co-Authored-By` line given in the session.
