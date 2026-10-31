# Writing MDEditor plugins

Plugins are small JavaScript programs that add commands, code formatters, event handlers and themes to
MDEditor. They run inside the editor (one isolated JavaScript engine per plugin) and can read and change the
open document. They have **no access to files or the network**.

## Quick start

1. Open **Settings → Plugins** and click **Create plugin…**. Give it a name.
2. MDEditor creates a folder with `manifest.json` and `main.js` and opens it.
3. Edit `main.js`, then tick the plugin in the list (or click **Reload** after changes).
4. Your commands appear in the **Plugins** menu. `console.log` output and errors appear under the plugin in
   Settings → Plugins.

Plugins live in the user's data folder (**Open plugins folder** in Settings shows it):

| System  | Folder |
|---------|--------|
| Windows | `%APPDATA%\MDEditor\MDEditor\plugins` |
| Linux   | `~/.local/share/MDEditor/MDEditor/plugins` |

Ready-made examples are in [`examples/plugins`](../examples/plugins). Copy a folder into the plugins folder
and enable it.

## manifest.json

```json
{
    "id": "word-count",
    "name": "Word Count",
    "version": "1.0.0",
    "author": "Your Name",
    "description": "Shows the number of words in the document.",
    "main": "main.js",
    "minAppVersion": "0.1.0"
}
```

`id` must be unique (it defaults to the folder name). `main` defaults to `main.js`. New plugins are disabled
until the user enables them.

## API

Two global objects are available: `app` and `editor`.

### `app.registerCommand(spec, fn)`

Adds an item to the **Plugins** menu.

```js
app.registerCommand({ id: "shout", name: "Shout", shortcut: "Ctrl+Alt+S" }, function () {
    editor.replaceSelection(editor.selectedText().toUpperCase());
});
app.registerCommand("Say hi", function () { editor.insert("hi"); }); // name only
```

### `app.registerFormatter(languages, fn)`

Registers a formatter used by **Format** (Shift+Alt+F) and **Format Document** for fenced code blocks in those
languages. It overrides the built-in formatter for the language. `fn(text, options)` gets the code and
`{ indent }` (spaces per level, from Settings) and returns the formatted code. Throw an `Error` to report a
problem; the message is shown in the status bar.

```js
app.registerFormatter(["sql"], function (text, options) {
    return text.replace(/\b(select|from|where)\b/gi, function (k) { return k.toUpperCase(); });
});
```

### `app.on(event, fn)`

| Event        | Arguments      | Notes |
|--------------|----------------|-------|
| `open`       | `(path)`       | after a file is opened |
| `beforeSave` | `(path, text)` | return a string to save that text instead (the editor is updated too) |
| `save`       | `(path)`       | after a file is written |

### `app.registerTheme(theme)`

Adds a theme to Settings → Appearance. Same format as a [theme JSON file](themes.md).

### `app.showMessage(text)`, `app.version()`

`showMessage` shows text in the status bar; `version` returns the MDEditor version.

### `editor`

Always refers to the document in the current tab. Every change is a single undo step.

| Function | Description |
|----------|-------------|
| `text()` / `setText(text)` | whole document |
| `selectedText()` / `replaceSelection(text)` | current selection (lines separated by `\n`) |
| `insert(text)` | insert at the cursor |
| `cursorPosition()` / `setCursorPosition(n)` | cursor offset in the text |
| `filePath()` | path of the file, empty for an unsaved note |

### `console`

`console.log`, `console.info`, `console.warn` and `console.error` write to the plugin's log in Settings.

## Limits

- Each call into a plugin may run for at most **3 seconds**, after which it is stopped and an error is logged.
- An exception in a plugin is logged and shown in Settings; it never closes MDEditor.
- The JavaScript engine is Qt's `QJSEngine` (ECMAScript 7). There is no `require`, DOM, `fetch` or file
  system access.
