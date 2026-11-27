# MDEditor

A lightweight, cross-platform Markdown editor with **live preview in a single pane**, inspired by Obsidian.
No split view and no vault: open any `.md` file from anywhere on disk and the text formats itself as you type.
The line under the cursor shows raw Markdown; every other line is rendered.

Written in C++20 with Qt 6 Widgets. Runs on Windows and Linux.

## Features

- Live preview: headings, **bold**, *italic*, ~~strikethrough~~, ==highlight==, `inline code`, fenced code blocks,
  quotes, bullet/ordered lists, task lists with clickable checkboxes, horizontal rules
- Any text file: Markdown files open with the live preview, other text files (code, config, logs, `.txt`) as
  plain text in a monospace font. View → Live Preview (Ctrl+E) switches per tab, View → Word Wrap (Alt+Z) too.
  Files that aren't text (images, PDFs, archives) open in their default application
- Obsidian-style right-click menu: open or copy a link, Add Link / Add External Link (Ctrl+K), Format (bold,
  italic, strikethrough, highlight, code, Clear Formatting), Paragraph (lists, task list, headings 1–6, body,
  quote), Insert (table, callout, code block, horizontal rule, footnote, symbol), Paste as Plain Text
  (Ctrl+Shift+V). Inside a table: add, duplicate, move and delete rows and columns, align a column, sort by a
  column, delete the table
- Tables rendered as an aligned grid (column alignment, inline formatting in cells); click a cell to edit
  the Markdown, Tab / Shift+Tab move between cells, Enter adds a row. The table button (top right) or
  Format → Insert → Table… opens a Word-style grid to insert an empty table
- Symbols and emoji: the Ω button next to the table button, or Format → Insert → Symbol or Emoji…, opens a drop-down that stays
  open while you insert several in a row, and closes when you click elsewhere, press Esc or leave the application
- Images: a line holding only `![alt](path)` or `![[path]]` shows the image, scaled to the text column.
  Set a width with `![[img.png|300]]` or `![alt|300](img.png)`. Paths are relative to the Markdown file.
  Dropping or pasting image files inserts links to them; pasting a screenshot or a copied image saves it
  as `assets/pasted-image-<timestamp>.png` next to the note and links it
- Math: `$inline$` and `$$display$$` TeX formulas rendered in place (MicroTeX), source shown while editing;
  `%%comments%%` dimmed. Format → Math / Comment, Insert → Math Block
- Syntax colors for code files and fenced code blocks (C/C++, C#, Java/Kotlin, JS/TS, Python, Go, Rust, SQL,
  shell, PowerShell, PHP, Ruby, Lua, CMake, Makefile, JSON, YAML, INI/TOML, CSS, XML/HTML, diff)
- Any encoding: UTF-8/16 with or without BOM and legacy code pages (Windows-1250 etc.) are detected and saved
  back unchanged; the status bar shows encoding and line endings and can reopen or save in another
- Spell checking (Hunspell) in several languages at once, Polish and English dictionaries included:
  suggestions, Add to Dictionary and Ignore in the right-click menu; languages in Settings → Editor
- Links: `[text](url)`, bare URLs and `[[wiki links]]`. Ctrl+click opens them (relative links resolve next to the current file)
- Smart lists: Enter continues the list, Enter on an empty item ends it, Tab / Shift+Tab indent
- Formatting shortcuts: Ctrl+B, Ctrl+I, Ctrl+\`, Ctrl+Shift+X (strike), Ctrl+Shift+H (highlight)
- Find and replace (Ctrl+F / Ctrl+H, F3 / Shift+F3): match case, whole word, regular expressions with `$1`
  groups, all matches highlighted, Replace All as one undo step
- Quick switcher (Ctrl+O): type part of a name to open any file in the folder, or Shift+Enter to create the note;
  the file dialog is Ctrl+Alt+O. Command palette (Ctrl+P): every menu command by name
- Right sidebar (Ctrl+Shift+B): Outline of the current note (Ctrl+Shift+L), Search in the folder (Ctrl+Shift+F, match
  case, regular expressions) and Tags (Ctrl+Shift+T) with their note counts, nested `#tags/like/this` included
- `#tags` shown as pills, YAML frontmatter shown as properties, `> [!note]` callouts as colored boxes (`[!tip]-`
  starts folded), footnotes `[^1]` as superscripts with the text on hover (Ctrl+click jumps to it)
- Folding: the chevron left of a heading, a list item with children or a callout hides what's under it; View →
  Toggle Fold (Ctrl+Shift+[), Fold All Headings, Unfold All (Ctrl+Shift+])
- `![[Note]]` and `![[Note#Heading]]` show that note (or section) in place; `[[links]]` find notes anywhere in the
  open folder
- Daily notes (File → Open Today's Note, Ctrl+Shift+D) and templates (Format → Insert → Template…) with
  `{{title}}`, `{{date}}`, `{{time}}` and `{{date:dddd, D MMMM}}`; folders and date format in Settings → Notes
- Graph view (View → Graph View, Ctrl+G): notes and their links, laid out by a force simulation; drag, zoom, click to
  open, or only the current note and its neighbors
- Word and character count in the status bar (of the selection, if any)
- Tabs (Ctrl+N new, Ctrl+W or middle click to close, Ctrl+Tab to switch); open tabs are restored on the next start
- File → Open Recent with the last 10 files
- Optional folder panel (Ctrl+Shift+E) to browse any folder, without indexing anything. Its right-click menu
  creates notes and folders, makes a copy, moves, renames (also F2, in place), deletes to the trash (also Del),
  copies paths and shows the file in the file manager; open tabs follow renamed and deleted files
- Tab right-click menu: close others / to the right / all, split right / down, move to the other pane, live
  preview, copy path, reveal in the folder panel, show in the file manager, open in the default app, bookmark,
  rename, move, delete
- Split view: two panes side by side or one above the other (View → Split Right / Down); the same note can be
  open in both, edits show in both at once; the layout is restored on the next start
- Bookmarks above the folder tree (right-click → Bookmark on files, folders and tabs; drag to reorder); drag
  files and folders in the tree to move them, or Move To…
- Files are saved exactly as typed: UTF-8 BOM and CRLF line endings are preserved, writes are atomic
- Settings (Ctrl+,): theme, editor font and size, line length, indentation, pasted images folder,
  formatting and plugins
- Themes for the whole app: System, Light, Dark, Obsidian, Solarized Light/Dark, Nord, Dracula, plus your own
  JSON themes ([docs/themes.md](docs/themes.md))
- Warns before overwriting a file changed elsewhere, and keeps working on slow [network drives](docs/network-drives.md)
- Connect a Google Drive folder from the app (File → Google Drive…), remembered across restarts and updates
- HTML entities such as `&rarr;` and `&#8594;` shown as their characters, in tables too
- Prettify (Shift+Alt+F) and Prettify Document (Ctrl+Shift+Alt+F), in the Format menu and the right-click menu: JSON (key order kept),
  XML/HTML, CSS, C/C++/Java/JavaScript/C#/Go (re-indent), Python (indentation), and Markdown table alignment
  (columns line up in the raw file in a monospace font, including CJK, emoji and combining marks).
  Uses clang-format, ruff/black, prettier or gofmt automatically when installed; optional format on save
- JavaScript plugins: commands, code formatters, events (open / before save / save) and themes, with a
  "Create plugin…" button ([docs/plugins.md](docs/plugins.md), [examples](examples/plugins))

## Installing

Download the latest release from the [Releases page](https://github.com/Gawello/MDEditor/releases):

- **Windows:** `MDEditor-<version>-win64.exe` (installer: Start menu and desktop shortcuts, "Open with" for
  `.md` files, uninstaller) or the portable `.zip`. The installer isn't code-signed, so SmartScreen may ask
  for confirmation ("More info" → "Run anyway").
- **Debian/Ubuntu:** `sudo apt install ./mdeditor_<version>_amd64.deb` (installs into `/opt/mdeditor` with
  its own Qt, adds the `mdeditor` command and a menu entry).
- **Other Linux:** `chmod +x MDEditor-<version>-x86_64.AppImage` and run it.

Help → Check for Updates… looks for a newer release (optionally at startup, once a day: Settings → Updates;
off by default). The Windows installer and the AppImage install it themselves (the setup runs, or the AppImage
is replaced in place); the `.deb` and the `.zip` link to the download. Packages are checked against the
SHA-256 digests GitHub publishes for release files.

Every CI run also uploads these packages as build artifacts. Releases are published automatically when a
tag `v<version>` matching the version in `CMakeLists.txt` is pushed.

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

Remote (http) images, syntax highlighting in code blocks, HTML/PDF export, translations.

## License

[MIT](LICENSE)
