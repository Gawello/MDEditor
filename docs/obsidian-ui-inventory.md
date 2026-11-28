# Obsidian UI inventory: every menu, panel, setting and command

A menu-by-menu record of Obsidian 1.15.1 (desktop, Linux), taken by running it with all 30 core plugins enabled
and opening each right-click menu, toolbar, panel, popup and settings page. Each entry shows what MDEditor has.
It's the detailed companion to [obsidian-features.md](obsidian-features.md), which holds priorities and the
release plan; new gaps found here are listed there too.

**MDEditor:** ✅ has it · 🟡 partly · ❌ missing · ➖ not planned. Shortcuts are Obsidian's defaults on Linux and
Windows (Ctrl stands for Cmd on macOS).

How it was gathered, so it can be redone for a new Obsidian version: run the AppImage on a virtual display with
`HOME`/`XDG_CONFIG_HOME` in a scratch folder and `--remote-debugging-port`, then read `app.commands.commands`,
`app.setting.settingTabs`/`pluginTabs` and the `.menu` elements after dispatching `contextmenu` events (submenus
open on `pointerover`). Typing goes through the DevTools `Input.insertText` call.

## 1. Editor right-click menu

The menu is built from groups; which groups appear depends on what's under the mouse.

### Always present

| Entry | MDEditor | Notes |
|---|---|---|
| Add link / Add external link | ✅ | |
| **Format ▸** Bold, Italic, Strikethrough | ✅ | |
| Format ▸ Highlight ▸ Default, Red, Orange, Yellow, Green, Blue, Purple | ✅ | Written `==🔴text==` (an emoji after `==`), as Obsidian 1.15 does |
| Format ▸ Code, Math, Comment | ✅ | |
| Format ▸ Clear formatting | ✅ | |
| **Paragraph ▸** Bullet list, Numbered list, Task list | ✅ | |
| Paragraph ▸ Heading 1–6, Body (current level checked) | 🟡 | No check mark on the current level |
| Paragraph ▸ Quote | ✅ | |
| **Insert ▸** Footnote, Table, Callout, Horizontal rule, Code block, Math block | ✅ | MDEditor also has Symbol or Emoji…, Template… |
| Insert ▸ New base | ❌ | Depends on Bases |
| Cut, Copy, Paste, Paste as plain text, Select all | ✅ | |
| Spelling suggestions, Add to dictionary | ✅ | Obsidian uses the system (Chromium) menu for these |

### Depends on the target

| Target | Entries | MDEditor |
|---|---|---|
| Selection | Search for “…” (opens search with the selected text) | ✅ |
| Selection | Extract current selection… (to a new note, leaves a link) | ❌ |
| Cursor in a section | Rename this heading… (updates links to it) | ❌ |
| Cursor in a section | Bookmark this heading… | ❌ |
| Cursor in a section | Extract this heading… (moves the section to a new note) | ❌ |
| Internal link `[[…]]` | Open in new tab / Open to the right / Open in new window | 🟡 no new window |
| Internal link | Edit link (dialog for target and display text) | ❌ |
| Internal link | Copy path ▸ as Obsidian URL / from vault folder / from system root | 🟡 Copy Link Address, Copy File Path |
| Internal link | Rename…, Move file to…, Bookmark… (act on the linked file) | 🟡 Rename |
| Internal link | Start presentation, Open in default app, Show in system explorer, Reveal file in navigation | 🟡 Reveal |
| External link | Open link in default browser / Open link in web viewer, Edit link, Copy URL | 🟡 Open Link, Copy Link Address |
| Tag `#tag` | Edit tag | ❌ |
| Footnote reference | Delete footnote and reference | ❌ |
| Embedded note `![[…]]` | Same as an internal link | ❌ |
| Image | Copy image, Swap file…, Remove image, Reset size, Delete image (the file) + the file entries | 🟡 no Swap file; open, copy, path, reveal, reset size, remove, delete; drag the edge to resize |
| Callout box | Edit, Callout type ▸ Info, Important, Tip, Success, Question, Warning, Example, Quote, Other…, None | ❌ |
| Table cell | Same as text; table commands are in the palette (below) | ✅ MDEditor has a Table submenu |
| Property name icon | Property type ▸ Checkbox, Date, Date & time, List, Number, Tags, Text; Cut, Copy, Paste; Remove | ❌ |
| Property value pill | Edit, Copy, Remove from list | ❌ |
| "Properties" heading | Add file property, Sort ▸ Name A–Z / Z–A, Clear file properties | ❌ |

## 2. Popups while typing

| Trigger | What it offers | MDEditor |
|---|---|---|
| `[[` | Every note and file (aliases shown as "Start → Home", folders after the name), unresolved links; footer hints: `#` heading, `^` block, `\|` display text | 🟡 no unresolved links, no hints |
| `[[Note#` | Headings of that note with their level (H1, H2…) | ✅ |
| `[[Note^` | Blocks (paragraphs, list items) of that note; picking one adds a `^id` | 🟡 `[[Note#^`: blocks that have an id |
| `[[^^` | Blocks of all notes | ❌ |
| `[[Note\|` | Display text suggestion | ❌ |
| `#` | Existing tags | ✅ |
| `/` (Slash commands) | Every command, filtered as you type, with shortcuts | ✅ |
| Ctrl+hover on a link | Page preview: the note rendered in a popover, scrollable, editable | 🟡 read-only |

## 3. File explorer (left sidebar)

| Where | Entries | MDEditor |
|---|---|---|
| Toolbar | New note, New folder | ✅ (right-click) |
| Toolbar | Change sort order ▸ File name A–Z / Z–A, Modified new–old / old–new, Created new–old / old–new | 🟡 no created time |
| Toolbar | Auto-reveal current file, Expand all / Collapse all, Show search filter | 🟡 always reveals; collapse all |
| Note | Open in new tab, Open to the right, Open in new window | ❌ |
| Note | Make a copy, Move file to…, Bookmark… | ✅ |
| Note | Merge entire file with… | ❌ |
| Note | Copy path ▸ as Obsidian URL / from vault folder / from system root | 🟡 absolute path only |
| Note | Open version history (File recovery), Start presentation | ❌ |
| Note | Open in default app, Show in system explorer, Rename…, Delete | ✅ |
| Folder | New note, New folder, New canvas, New base | 🟡 note and folder |
| Folder | Make a copy, Move folder to…, Bookmark…, Rename…, Delete, Copy path, Show in system explorer | ✅ |
| Folder | Search in folder | ✅ |
| Several selected (Ctrl/Shift+click) | New folder with selection (N items), Move N items to…, Bookmark…, Delete | 🟡 move, bookmark, delete |
| Empty area | New note, New folder, New canvas, New base | 🟡 |
| — | MDEditor extras: Open as Root Folder, Open Parent Folder | ✅ |

## 4. Tabs and note header

| Where | Entries | MDEditor |
|---|---|---|
| Tab right-click | Close | ✅ |
| Tab right-click | Pin | ❌ |
| Tab right-click | Link with tab… (linked panes) | ❌ |
| Tab right-click | Backlinks in document, Reading view, Source mode | ❌ |
| Tab right-click | Move to new window, Open in new window | ❌ |
| Tab right-click | Split right, Split down | ✅ |
| Tab right-click | Rename…, Move file to…, Delete file, Copy path ▸, Open in default app, Show in system explorer, Reveal file in navigation | ✅ |
| Tab right-click | Bookmark…, Merge entire file with…, Add file property, Export to PDF…, Find…, Replace…, Open version history, Start presentation | ❌ (Find/Replace are in the Edit menu) |
| Tab right-click | Open linked view ▸ local graph, backlinks, outgoing links, file properties, outline | ❌ |
| Tab right-click | — MDEditor extras: Close Others, Close Tabs to the Right, Close All, Move to Other Pane | ✅ |
| Tab bar | New tab (+) | ✅ |
| Tab bar ▾ menu | Stack tabs, Bookmark N tabs…, Close all, list of open tabs | 🟡 Close All only |
| Note header | ← → (history), file path breadcrumbs, Bookmark star, mode button (editing ↔ reading; Ctrl+click opens to the right), ⋮ More options (= tab menu without Close/Pin) | ❌ |
| Status bar | Backlink count, property count, editing mode (click to switch), words + characters, sync status | 🟡 backlinks, words, encoding, line ending |

## 5. Sidebar panels

| Panel | Controls | MDEditor |
|---|---|---|
| Search | Match case, regex via `/…/`, settings: Collapse results, Show more context, Explain search terms, sort (as the explorer); operator hints when empty: `path:` `file:` `tag:` `line:` `section:` `[property]` (also `content:`, `block:`, `task:`, `task-todo:`, `task-done:`, `match-case:`, `ignore-case:`, `OR`, `-`, `"…"`); right-click a result: file menu | 🟡 text, match case, regex, tag results |
| Bookmarks | Bookmark the active tab, New group, Collapse all, filter; bookmarks for files, folders, searches, headings, blocks, graphs; groups | 🟡 files and folders |
| Backlinks | Linked mentions, Unlinked mentions (each with a Link button), Collapse results, Show more context, sort, filter | 🟡 no collapse/context/sort/filter |
| Outgoing links | Links (resolved and unresolved, with headings), Unlinked mentions | 🟡 no unlinked mentions |
| Outline | Filter, Auto-scroll to current section, Collapse all; drag headings to move sections | 🟡 no filter, no drag |
| Tags | Sort (name A–Z/Z–A, frequency high/low), Show nested tags, Expand all, filter | 🟡 list with counts |
| All properties | Every property with its count, sort, filter | ❌ |
| File properties | The note's properties, with type warnings | ❌ |
| Footnotes | All footnotes of the note | ❌ |
| All panels | ← → history, Bookmark, ⋮ More options; right-click a sidebar tab: Close; drag between sidebars | ❌ |

## 6. Graph view

| Group | Controls | MDEditor |
|---|---|---|
| Filters | Search files, Tags, Attachments, Existing files only, Orphans | ✅ (path words or tag:) |
| Groups | Query + color, any number | ❌ |
| Display | Arrows, Text fade threshold, Node size, Link thickness, Animate (timelapse) | 🟡 arrows |
| Forces | Center force, Repel force, Link force, Link distance | ✅ |
| Other | Restore default settings; ⋮ Split right/down, Copy screenshot, Bookmark; local graph with depth and the same controls | 🟡 restore defaults, local graph with depth |

## 7. Canvas and Bases

| View | Entries | MDEditor |
|---|---|---|
| Canvas toolbar | Drag to add card / note from vault / media from vault, Canvas settings, Zoom in/out, Reset zoom, Zoom to fit (Shift+1), Undo, Redo, Help | ❌ |
| Canvas background | Add card, Add note from vault, Add media from vault, Add web page, Create group, Paste, Snap to grid, Snap to objects, Read-only | ❌ |
| Canvas card | Zoom to selection, Edit, Convert to file…, Remove (plus color and alignment in the card toolbar) | ❌ |
| Canvas commands | Create new canvas, Convert to file, Export as image, Jump to group | ❌ |
| Bases toolbar | View switcher (Table, Cards…), result count, Display, sort, filter, properties | ❌ |
| Bases column header | Hide column, Summarize…, Sort A→Z / Z→A, Edit property… | ❌ |
| Bases row | File menu | ❌ |

## 8. Ribbon (left icon bar)

Quick switcher, Graph view, New canvas, Today's daily note, Insert template, Command palette, New base, New unique
note, Random note, Start/stop recording, Manage workspace layouts, Publish changes; the vault switcher (vaults,
Manage vaults…), Help and Settings at the bottom. MDEditor: ➖ (menus and the palette cover it).

## 9. Commands (227, all reachable from Ctrl+P, `/` and the Hotkeys settings)

Default shortcuts in brackets. Commands of paid services (Sync, Publish), mobile, and the vault switcher are left
out.

### Editing

| Command | MDEditor |
|---|---|
| Toggle bold [Ctrl+B], italic [Ctrl+I], strikethrough, highlight, code, inline math, comment [Ctrl+/] | ✅ (comment has no shortcut) |
| Toggle red / orange / yellow / green / blue / purple highlight | ✅ |
| Clear formatting | ✅ |
| Toggle bullet list, numbered list, blockquote; Set as heading 1–6, Toggle heading, Remove heading | ✅ |
| Toggle checkbox status [Ctrl+L], Cycle bullet/checkbox | ✅ |
| Indent / Unindent list item | ✅ (Tab / Shift+Tab) |
| Move line up / down | ✅ (Alt+↑/↓) |
| Delete paragraph [Ctrl+D] | ✅ |
| Add cursor above / below | ❌ |
| Insert callout, code block, footnote, horizontal rule, Markdown link [Ctrl+K], math block, table, internal link, embed, tag, attachment | 🟡 no "insert tag/embed/attachment" |
| Fold more / Fold less / Toggle fold on the current line / Fold all headings and lists / Unfold all / Toggle fold properties | 🟡 toggle, fold all, unfold all |
| Follow link under cursor [Alt+Enter], open it in a new tab [Ctrl+Enter], to the right [Ctrl+Alt+Enter], in a new window | ✅ (no new window) |
| Rename this heading… | ❌ |
| Show context menu under cursor | ❌ (the Menu key works) |
| Search current file [Ctrl+F], Search & replace [Ctrl+H] | ✅ |
| Save current file [Ctrl+S] | ✅ (MDEditor saves only on demand; Obsidian saves on its own) |
| Table: add row above/below, duplicate row, delete row, move row up/down, add column left/right, duplicate column, delete column, move column left/right, align left/center/right | ✅ (no "duplicate column") |
| Toggle line numbers, readable line length, spellcheck, Live Preview/Source mode, reading view [Ctrl+E], default mode for new tabs | 🟡 readable line length is a setting; spellcheck in settings |
| Insert current date / time (Templates) | ❌ |
| Add alias, Add file property [Ctrl+;], Clear file properties | ❌ |
| Download attachments for current file | ❌ |

### Files, tabs and windows

| Command | MDEditor |
|---|---|
| Create new note [Ctrl+N], in current tab, to the right [Ctrl+Shift+N] | 🟡 new tab |
| New tab [Ctrl+T], Close current tab [Ctrl+W], Close all other tabs, Close others in tab group, Close this tab group | ✅ |
| Undo close tab [Ctrl+Shift+T] | ✅ |
| Go to tab #1–8 [Ctrl+1…8], last tab [Ctrl+9], next/previous tab [Ctrl+Tab, Ctrl+PgDn / Ctrl+Shift+Tab, Ctrl+PgUp] | ✅ |
| Navigate back [Ctrl+Alt+←] / forward [Ctrl+Alt+→] | ✅ (also Alt+←/→) |
| Focus on last note, Focus on tab group left/right/above/below | ❌ |
| Split right, Split down | ✅ |
| Toggle pin, Toggle stacked tabs | ❌ |
| New window, Move current tab to new window, Open current tab in new window, Close window [Ctrl+Shift+W], Toggle window always on top | ❌ |
| Rename file [F2], Move current file to another folder, Make a copy, Delete current file, Show trash | 🟡 no "show trash" |
| Copy file path (from system root / from vault folder), Copy Obsidian URL | 🟡 absolute path |
| Open in default app, Show in system explorer, Reveal current file in navigation | ✅ |
| Export to PDF… | ✅ |
| Toggle left sidebar / right sidebar / ribbon | ✅ (no ribbon) |
| Zoom in / out / reset, Increase / decrease font size | 🟡 no reset |
| Change theme…, Toggle light/dark mode | 🟡 in Settings only |
| Open settings [Ctrl+,], Open help [F1], Show release notes, Show debug info, Reload app without saving | 🟡 Settings without a shortcut |

### Core plugin commands

| Plugin | Commands | MDEditor |
|---|---|---|
| Backlinks | Show backlinks, Open backlinks for the current note, Toggle backlinks in document | 🟡 show |
| Outgoing links | Show outgoing links, Open for the current file | ✅ |
| Outline | Show outline, Open outline of the current file | ✅ |
| Tags view, Properties view, Footnotes view | Show tags / all properties / file properties / footnotes | 🟡 tags |
| Graph view | Open graph view [Ctrl+G], Open local graph, Start timelapse animation | 🟡 no timelapse |
| Search | Search in all files [Ctrl+Shift+F] | ✅ |
| Quick switcher | [Ctrl+O] | ✅ |
| Command palette | [Ctrl+P] | ✅ |
| Bookmarks | Bookmark…, Bookmark all tabs…, heading / block under cursor…, current search…, Remove bookmark for the current file, Show bookmarks | 🟡 file and folder |
| Daily notes | Open today's / next / previous daily note | 🟡 today only |
| Templates | Insert template, Insert current date / time | 🟡 template |
| Note composer | Extract this heading…, Extract current selection…, Merge current file with another file… | ❌ |
| Unique note creator | Create new unique note, Add unique internal link | ❌ |
| Random note | Open random note | ❌ |
| File recovery | Open local history | ✅ File → Version History |
| Workspaces | Save layout, Load workspace layout, Manage, Save and load another | ❌ |
| Canvas | Create new canvas, Convert to file…, Export as image, Jump to group | ❌ |
| Bases | Create new base, Insert new base, Add item, Add view, Switch view…, Copy table to clipboard | ❌ |
| Slides | Start presentation | ❌ |
| Audio recorder | Start / stop recording audio | ❌ |
| Web viewer | Open web viewer, Focus address bar, Search the web, Show history, Save to vault, Toggle reader mode, Zoom in/out/reset | ❌ |
| Format converter | Frontmatter migration | ➖ |

## 10. Settings, page by page

### General
Version and Check for updates ✅; Automatic updates ✅ (off by default here); Receive early access versions ❌;
Language ❌; Help ❌; Account and licenses ➖; Notify if startup takes long ➖; Command line interface 🟡; Hardware
acceleration ➖.

### Appearance
Base color scheme (dark / light / adapt to system) ✅; Accent color 🟡 per theme; Themes ✅ and community themes ❌;
CSS snippets ➖; Interface font ❌, Text font ✅, Monospace font ❌; Font size ✅; Quick font size adjustment
(Ctrl+wheel) ❌; Custom app icon ➖.

### Interface
Show tab title bar ❌; Show ribbon ➖; Zoom level 🟡 (View → Zoom); Native menus ➖; Window frame style ➖; Open
settings in new window ➖.

### Editor
| Setting | MDEditor |
|---|---|
| Default view for new tabs (editing / reading) | ❌ |
| Default editing mode (Live Preview / Source) | 🟡 per file type |
| Show editing mode in status bar | ❌ |
| Inline title | ✅ |
| Readable line length | ✅ |
| Strict line breaks | ❌ |
| Properties in document (visible / hidden / source) | ❌ |
| Fold heading, Fold indent | 🟡 always on |
| Line numbers | ✅ |
| Indentation guides | ❌ |
| Right-to-left | ❌ |
| Spellcheck, Spellcheck languages | ✅ |
| Auto-pair brackets, Auto-pair Markdown syntax | ✅ |
| Smart lists | ✅ always on |
| Indent using tabs, Indent visual width | 🟡 |
| Convert pasted HTML to Markdown (Ctrl+Shift+V pastes without) | ✅ |
| Vim key bindings | ❌ |

### Files and links
| Setting | MDEditor |
|---|---|
| Default file to open (last opened / new note / specific file / daily note) | 🟡 last session |
| Always focus new tabs | ❌ |
| Default location for new notes (root / same folder / given folder) | ✅ |
| Default location for new attachments (root / same folder / subfolder / given folder) | ✅ (subfolder, same folder, folder in the root) |
| New link format (shortest / relative / absolute) | ❌ |
| Automatically update internal links | ✅ (ask / always / never) |
| Use [[Wikilinks]] | ❌ |
| Show all file types | ✅ |
| Confirm before deleting files | ✅ |
| Delete attachments when deleting files (ask / always / never) | ❌ |
| Deleted files (system trash / `.trash` / permanently) | 🟡 system trash |
| Excluded files | ✅ |
| Allow URI callbacks | ❌ |
| Rebuild vault cache | ❌ (the index refreshes on activation) |

### Hotkeys
Search by name or key, filter assigned, every command assignable, several keys per command, conflict warnings,
reset to default. MDEditor ✅ for the menu commands (one key each; extra default keys are kept until changed).

### Core plugin settings
| Plugin | Settings | MDEditor |
|---|---|---|
| Quick switcher | Show existing only, Show attachments, Show all file types | ❌ |
| Backlinks | Show backlinks at the bottom of notes | ❌ |
| Page preview | Require Ctrl for hover in: search/backlinks/outgoing, reading view, editing view, tab header, files, properties, bookmarks, outline, bases, graph | ❌ |
| Daily notes | Date format (presets or custom), New file location, Template | ✅ |
| Templates | Folder, Date format, Time format | 🟡 folder |
| Note composer | Text after extraction (link / embed / none), Template, Confirm merge | ❌ |
| Command palette | Pinned commands | ❌ |
| File recovery | Snapshot interval (min), History length (days), View snapshots, Clear history | 🟡 no Clear history |
| Unique note creator | Folder, Template, Prefix format | ❌ |
| Canvas | New file location, Mouse wheel (pan/zoom), Ctrl+drag action, Show card names, Snap to grid/objects, Zoom threshold | ❌ |
| Web viewer | Open external links inside, Homepage, Saved page folder, Search engine, Ad blocker and lists | ❌ |
| Core plugins | Every feature above can be turned off | ❌ |
| Community plugins | Restricted mode, browse, install, update | 🟡 local plugins |
