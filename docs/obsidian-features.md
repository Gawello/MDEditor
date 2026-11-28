# Obsidian feature map and long-term roadmap

Everything Obsidian (desktop, version 1.15.1) offers, compared with MDEditor, as the long-term plan for the app.
The list comes from Obsidian 1.15.1 itself (its core plugins and every option in its settings) and from its
documentation. Update the status column whenever a feature lands. Every right-click menu, panel control, popup,
command and setting is listed entry by entry in [obsidian-ui-inventory.md](obsidian-ui-inventory.md); the rows
below are the features those entries add up to.

**Status:** ✅ done · 🟡 partly done · ❌ missing · ➖ not planned (a paid service, platform-specific, or
against a product decision in [CLAUDE.md](../CLAUDE.md)).
**Priority:** P1 next, P2 later, P3 nice to have. **Effort:** S (days), M (a week or two), L (a month or more).

Product decisions that shape this list: no vaults (any folder, any file), one live-preview pane per tab, and
any text file can be edited, not just Markdown.

## 1. Markdown syntax and rendering

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Live Preview (rendered, markers on the cursor line) | ✅ | Core of the app | | |
| Source mode (raw Markdown, still highlighted) | 🟡 | Ctrl+E switches to plain text without colors | P2 | S |
| Reading view (rendered, read-only, no markers anywhere) | ❌ | | P2 | M |
| Headings, bold, italic, strikethrough, `==highlight==`, inline code | ✅ | | | |
| Colored highlights (red, orange, yellow, green, blue, purple; new in 1.15) | ❌ | Format ▸ Highlight ▸ submenu | P3 | S |
| Bullet, numbered and task lists, nested | ✅ | | | |
| Custom task statuses (`[/]`, `[-]`, `[>]`…) | ❌ | Rendered as icons by many themes | P3 | S |
| Tables with rich editing | ✅ | Row/column menus, sort, alignment, Prettify | | |
| Code blocks with syntax colors | ✅ | ~22 languages | | |
| Math (`$…$`, `$$…$$`, MathJax) | 🟡 | MicroTeX covers most LaTeX; no MathJax extensions | P3 | M |
| Mermaid diagrams | ❌ | Needs a renderer (JS engine or native port) | P2 | L |
| Callouts (`> [!type]`, foldable `+`/`-`) | ✅ | Missing: icons, nested callouts, custom types | P2 | S |
| Comments `%%…%%` | ✅ | | | |
| Footnotes `[^1]` | ✅ | Superscript, hover text, Ctrl+click to definition | | |
| Inline footnotes `^[text]` | ❌ | | P3 | S |
| Horizontal rules, blockquotes | ✅ | | | |
| HTML character references (`&rarr;`) | ✅ | | | |
| Inline HTML (`<span>`, `<details>`, `<iframe>`) | ❌ | At least `<br>`, `<sub>`, `<sup>`, `<u>`, `<kbd>` | P3 | M |
| Images `![](…)`, `![[…]]`, with width `\|300` | ✅ | Local files only | | |
| Remote (http) images | ❌ | Needs QtNetwork downloads + cache | P2 | S |
| Embedded notes `![[Note]]` and sections `![[Note#Heading]]` | ✅ | | | |
| Embedded blocks `![[Note#^id]]` | ❌ | Depends on block references | P2 | S |
| Embedded PDF, audio, video | ❌ | PDF needs QtPdf; audio/video QtMultimedia | P3 | M |
| Embedded search results (` ```query `) | ❌ | | P3 | M |
| Strict line breaks option | ❌ | Rendering-only option | P3 | S |
| Right-to-left text | ❌ | | P3 | S |

## 2. Editing

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Smart lists (continue, indent with Tab) | ✅ | | | |
| Auto-pair brackets and quotes | ❌ | | P1 | S |
| Auto-pair Markdown syntax (`**`, `` ` ``, `==`) | ❌ | | P1 | S |
| Fold headings | ✅ | | | |
| Fold indentation (lists) | ✅ | | | |
| Line numbers | ❌ | Useful for code files | P2 | S |
| Indentation guides for lists | ❌ | | P3 | S |
| Indent with tabs or spaces, tab width | 🟡 | Width setting only | P3 | S |
| Spell checking | ✅ | Hunspell, several languages | | |
| Multiple cursors (Alt+click, Ctrl+D…) | ❌ | Hard in QPlainTextEdit | P3 | L |
| Vim key bindings | ❌ | | P3 | L |
| Slash commands (`/` menu in the text) | ❌ | Reuse the command palette | P2 | S |
| Convert pasted HTML to Markdown | ❌ | Pasting from browsers and Word | P1 | M |
| Paste and drop images (saved as attachments) | ✅ | | | |
| Drop files to link them | 🟡 | Images only; other files should get links too | P2 | S |
| Find and replace with regular expressions | ✅ | | | |
| Inline title (file name editable above the note) | ❌ | Renames the file | P2 | S |
| Formatting commands and toolbar (Format menu) | ✅ | Plus right-click menus | | |
| Word and character count | ✅ | | | |
| Move line up / down, delete paragraph (Ctrl+D) | ❌ | | P2 | S |
| Toggle checkbox (Ctrl+L), cycle bullet → checkbox → plain | ❌ | Clicking the box works | P2 | S |
| Follow the link under the cursor from the keyboard (Alt+Enter; Ctrl+Enter in a new tab) | ❌ | | P2 | S |
| Insert current date / time | ❌ | Template date formats | P3 | S |
| "Search for “selection”" in the right-click menu | ❌ | Opens folder search | P2 | S |
| Image right-click menu: copy image, swap file, remove, delete file; resize by dragging | ❌ | Width goes into `\|300` | P2 | M |
| Callout right-click: change type (Info, Tip, Warning…), edit | ❌ | | P3 | S |
| Edit link / edit tag dialogs, "Delete footnote and reference" | ❌ | | P3 | S |
| Prettify / format code | ✅ | MDEditor extra: JSON, XML, CSS, C-like, Python, tables | | |

## 3. Links and knowledge graph

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| `[[Wiki links]]` and `[text](path.md)`, Ctrl+click opens | ✅ | Resolved anywhere in the open folder | | |
| Link autocomplete while typing `[[` | ❌ | Notes, then `#` headings and `^` blocks | P1 | M |
| Links to headings `[[Note#Heading]]` | 🟡 | Opens the note, doesn't scroll to the heading | P1 | S |
| Block references `^id` and `[[Note#^id]]` | ❌ | | P2 | M |
| Aliases (`[[Note\|alias]]`, `aliases:` property) | 🟡 | Display text works; `aliases:` not used for finding notes | P2 | S |
| Click a link to a missing note to create it | ❌ | | P1 | S |
| Update links when a file is renamed or moved | ❌ | Ask or update automatically, as in Obsidian | P1 | M |
| Rename a heading and update the links to it | ❌ | Right-click "Rename this heading…" | P2 | M |
| Link right-click: open in new tab / to the right / in new window; act on the target (rename, move, bookmark, reveal) | 🟡 | Open Link and Copy Link Address only | P2 | S |
| New link format option (shortest / relative / absolute), wiki or Markdown links | ❌ | | P2 | S |
| Backlinks (linked mentions), also under the note | ❌ | The index has everything it needs | P1 | M |
| Unlinked mentions (with "Link" button) | ❌ | | P2 | M |
| Outgoing links pane | ❌ | | P1 | S |
| Page preview on hover (Ctrl+hover on a link) | ❌ | Reuse the embed renderer | P1 | M |
| Graph view | ✅ | Force layout, drag, zoom, open by click | | |
| Local graph with depth | 🟡 | Depth 1 only | P2 | S |
| Graph filters (tags, attachments, orphans, existing files), groups with colors, forces settings, arrows | ❌ | | P2 | M |
| Graph timelapse animation | ❌ | | P3 | S |
| Tags `#tag`, nested `#a/b` | ✅ | Pills in the text, Tags pane with counts | | |
| Tag autocomplete | ❌ | | P2 | S |
| Rename a tag everywhere | ❌ | | P3 | M |

## 4. Properties (frontmatter)

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| YAML frontmatter shown as properties | ✅ | Read-only rendering; raw YAML on the cursor | | |
| Property editor with types (text, list, number, checkbox, date, date & time) | ❌ | Add/remove/edit fields with widgets; right-click to change the type, sort, clear; Add property (Ctrl+;) | P2 | L |
| Property name and value autocomplete | ❌ | | P2 | S |
| Properties view (all properties in the folder, counts) | ❌ | | P2 | S |
| `tags`, `aliases`, `cssclasses` special properties | 🟡 | `tags` counted; `aliases` not used | P2 | S |
| Show properties: visible / hidden / source | ❌ | | P3 | S |

## 5. Navigation and search

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Quick switcher (Ctrl+O), create a note from it | ✅ | | | |
| Quick switcher: aliases, headings (`#`), attachments filter | ❌ | | P2 | S |
| Command palette (Ctrl+P) | ✅ | | | |
| Pinned commands, recently used first | ❌ | | P3 | S |
| Search in the folder | ✅ | Text, match case, regex, tag results | | |
| Search operators (`path:`, `file:`, `tag:`, `line:`, `section:`, `block:`, `task:`, `[property:value]`) | ❌ | | P2 | M |
| Explain search, sort results, more context, copy results | ❌ | | P3 | S |
| Navigation history: Back / Forward (Alt+←/→) | ✅ | Window-wide, across tabs and panes; mouse side buttons | | |
| Bookmarks: files and folders | ✅ | | | |
| Bookmarks: searches, headings, graphs, groups | ❌ | | P3 | M |
| Outline (table of contents, auto-scroll to the current section) | ✅ | | | |
| Outline: filter, collapse all, drag a heading to move its section | ❌ | | P3 | M |
| Tags pane: sort by name / frequency, nested or flat, filter | 🟡 | Sorted list with counts | P3 | S |
| Footnotes pane, file properties pane, all properties pane | ❌ | | P3 | S |
| Random note | ❌ | | P3 | S |
| Recent files | ✅ | File → Open Recent | | |
| Default file to open at startup (last session / new note / a file / daily note) | 🟡 | Restores the last session | P3 | S |

## 6. Files and folders

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| File explorer: create, rename, move (drag), duplicate, delete | ✅ | | | |
| Sort by name / modified / created | ❌ | Name only | P2 | S |
| Explorer toolbar: auto-reveal current file, expand / collapse all, filter | ❌ | | P2 | S |
| Select several files (Ctrl/Shift+click): move, delete, bookmark, new folder with them | ❌ | | P2 | M |
| "Search in folder" on a folder | ❌ | Fills the search panel's scope | P2 | S |
| Copy path relative to the open folder, as an app URL | 🟡 | Absolute path only | P3 | S |
| Multiple vaults, vault switcher | ➖ | No vaults by design; a list of recent folders would help | P2 | S |
| Default location for new notes / attachments, attachment subfolder | 🟡 | Attachment folder name only | P2 | S |
| Excluded files (hidden from search, graph, switcher) | ❌ | | P2 | S |
| Delete to system trash / `.trash` folder / permanently, confirm deletion | 🟡 | System trash with a fallback; no `.trash` option | P3 | S |
| Delete attachments with the note | ❌ | | P3 | S |
| Show all file types | ✅ | Every text file opens; binary files go to their app | | |
| Open in default app, reveal in file manager, copy path | ✅ | | | |
| File recovery (snapshots of notes, browse and restore) | ❌ | A safety net worth having early | P1 | M |
| Note composer (merge notes, extract selection to a new note) | ❌ | | P2 | M |
| Unique note creator (timestamp prefix, Zettelkasten) | ❌ | | P3 | S |
| Format converter / importer (Roam, Notion, Bear, Evernote…) | ❌ | | P3 | L |
| Download remote attachments into the folder | ❌ | | P3 | S |

## 7. Workspace and layout

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Tabs, reorder, close others, restore session | ✅ | | | |
| Split panes | 🟡 | Two panes (right or down); Obsidian allows any number | P2 | M |
| Undo close tab (Ctrl+Shift+T) | ✅ | | | |
| Go to tab 1–8 (Ctrl+1…8), last tab (Ctrl+9) | ✅ | | | |
| Tab list drop-down (all open tabs, close all, bookmark all) | ❌ | | P3 | S |
| "Open linked view" for a note: its local graph, backlinks, outline, properties beside it | ❌ | | P3 | S |
| Status bar: backlink and property counts, editing mode switch | 🟡 | Words, encoding and line endings shown | P3 | S |
| Window always on top | ❌ | | P3 | S |
| Pinned tabs | ❌ | | P3 | S |
| Stacked tabs | ❌ | | P3 | M |
| Linked panes (one follows the other) | ❌ | | P3 | M |
| Pop-out windows | ❌ | | P2 | M |
| Left and right sidebars with tabs | ✅ | Folder panel + bookmarks left; outline, search, tags right | | |
| Drag views between sidebars | ❌ | | P3 | M |
| Workspaces (save and restore layouts) | ❌ | | P3 | M |
| Tab title bar / breadcrumbs, "editing mode" in the status bar | ❌ | | P3 | S |
| Ribbon (icon bar) | ➖ | Menus and the palette cover it | | |
| Full screen / focus mode | ❌ | | P3 | S |

## 8. Bigger views (core plugins)

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Canvas (`.canvas`, JSON Canvas: cards, notes, media, groups, arrows) | ❌ | Same file format, so files work in both apps | P2 | L |
| Bases (`.base`: table and card views of notes by properties, filters, formulas) | ❌ | Needs typed properties first | P2 | L |
| Slides (presentation from `---`-separated notes) | ❌ | | P3 | M |
| Audio recorder (record into an attachment) | ❌ | QtMultimedia | P3 | M |
| Web viewer (open web links in a tab, save as note) | ❌ | Needs QtWebEngine (large) | P3 | L |
| PDF viewer with annotations links | ❌ | QtPdf | P3 | M |
| Footnotes view (all footnotes of the note) | ❌ | | P3 | S |
| Daily notes (folder, date format, template) | ✅ | | | |
| Templates (folder, `{{title}}`, `{{date}}`, `{{time}}`) | ✅ | | | |
| Calendar for daily notes | ❌ | Community plugin in Obsidian; cheap here | P3 | S |

## 9. Appearance and settings

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Light / dark / system, themes | ✅ | 8 built-in, JSON themes | | |
| Accent color | 🟡 | Per theme, not a separate setting | P3 | S |
| Interface font, text font, monospace font | 🟡 | Text font only | P3 | S |
| Font size, quick size change (Ctrl+wheel), zoom level | 🟡 | Font size and Zoom In/Out | P3 | S |
| Readable line length | ✅ | | | |
| Community theme browser | ❌ | | P3 | M |
| CSS snippets | ➖ | Qt widgets, not HTML; themes are JSON | | |
| Customizable hotkeys (search, conflicts) | ❌ | Expected by power users | P1 | M |
| Interface language (translations) | ❌ | Polish first; Qt Linguist | P2 | M |
| Native menus, window frame, translucency, custom app icon | ➖ | | | |

## 10. Extensibility and integration

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Plugins | 🟡 | JavaScript plugins: commands, formatters, events, themes; much smaller API | P2 | L |
| Community plugin browser and updates | ❌ | Needs a plugin registry | P3 | L |
| Restricted mode (plugins off) | ✅ | Plugins are off until enabled | | |
| `obsidian://` URI scheme (open, new, search) | ❌ | `mdeditor://` | P3 | S |
| Command-line interface | 🟡 | Opens files given on the command line | P3 | S |
| Export to PDF | ❌ | `QTextDocument` / `QPdfWriter`; one of the most used features | P1 | M |
| Export to HTML, print, copy as HTML | ❌ | | P2 | M |

## 11. Services and platforms

| Obsidian | MDEditor | Notes | Prio | Effort |
|---|---|---|---|---|
| Sync (paid, end-to-end encrypted, version history) | ➖ | Google Drive through rclone instead | | |
| Publish (paid website hosting) | ➖ | An HTML export could cover simple cases | | |
| Mobile apps (Android, iOS), mobile toolbar | ➖ | Desktop only for now | | |
| Account, licenses, keychain for secrets | ➖ | | | |
| Automatic updates | ✅ | Windows setup and AppImage install themselves | | |
| Help vault, "What's new" after an update | ❌ | Show the release notes once after updating | P3 | S |

## MDEditor features Obsidian doesn't have

Edits any text file (code, config, logs) with syntax colors; keeps the encoding, BOM and line endings of legacy
files (Windows-1250 and others); Prettify for code and tables; table and symbol/emoji pickers; works on slow and
network drives without freezing; Google Drive folder through rclone; no vault needed.

## Proposed order

Each milestone is a release (minor version), so the in-app updater delivers it.

1. **0.4 – Links that work both ways**: Back/Forward history, backlinks and outgoing links panes, `[[` autocomplete
   (notes, headings), heading links that scroll, create a note from a missing link, update links on rename/move,
   page preview on hover, link right-click menu (open in new tab / to the right, act on the target), undo close tab.
2. **0.5 – Writing comfort**: auto-pair brackets and Markdown, convert pasted HTML, slash commands, tag and
   property autocomplete, inline title, line numbers, drop any file as a link, remote images, move line up/down,
   toggle checkbox (Ctrl+L), follow link from the keyboard, "Search for selection", image menu and resizing.
3. **0.6 – Safety and power use**: file recovery snapshots, customizable hotkeys, export to PDF (then HTML and
   print), excluded files, new note and attachment locations, explorer sorting, auto-reveal, multi-selection and
   "Search in folder", Ctrl+1…9 for tabs, recent folders.
4. **0.7 – Structure**: block references and block embeds, typed property editor and Properties view, search
   operators, graph filters/groups/depth, note composer, Reading view.
5. **0.8 – Layout**: any number of split panes, pop-out windows, pinned tabs, workspaces, bookmarks for searches
   and headings, interface translation (Polish).
6. **1.0 and later – Big views**: Canvas (JSON Canvas), Bases, Slides, Mermaid, PDF viewer and embeds, audio
   recorder, plugin API growth and a plugin/theme browser.
