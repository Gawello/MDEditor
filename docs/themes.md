# Themes

Choose a theme in **Settings → Appearance**. Built-in presets: System (follows the operating system), Light,
Dark, Obsidian, Solarized Light, Solarized Dark, Nord and Dracula.

## Custom themes

Save a `.json` file in the themes folder (**Open themes folder** in Settings), then click **Reload themes**:

```json
{
    "id": "forest",
    "name": "Forest",
    "dark": true,
    "colors": {
        "window": "#1b221d",
        "base": "#222b25",
        "text": "#d8e3d9",
        "accent": "#7fbf7f",
        "codeText": "#e6c07b",
        "codeBackground": "#2c362f",
        "highlight": "#5a5320"
    }
}
```

| Color            | Used for |
|------------------|----------|
| `window`         | panels, tab bar, menus, dialogs |
| `base`           | editor and list backgrounds |
| `text`           | all text |
| `accent`         | links, selection, checkboxes, quote bars, focus |
| `codeText`       | inline code and code blocks |
| `codeBackground` | code background |
| `highlight`      | `==marked text==` background |

Colors you leave out are taken from the built-in Light or Dark theme, depending on `dark`. Plugins can add
themes too, with `app.registerTheme` (see [plugins.md](plugins.md)).

Custom themes use Qt's Fusion style, so the whole application follows the theme colors. The System theme uses
the native look of the operating system.
