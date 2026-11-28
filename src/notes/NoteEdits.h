#pragma once

#include <QString>

#include <functional>
#include <optional>

// Changes to notes that aren't open in a tab (linking a mention, updating links after a rename): read, change,
// write back with the file's own encoding, BOM and line endings, and nothing else (no format-on-save, no plugin
// hooks: the user isn't editing these files).
namespace NoteEdits {

// Returns the new text, or nothing to leave the file alone. Runs off the GUI thread: capture by value.
using Change = std::function<std::optional<QString>(const QString &text)>;

struct Outcome {
    QString path;
    bool changed = false;
    QString error; // empty when it worked (or there was nothing to change)
    QString text;  // the text written, when changed
};

// Touches the disk: call it from a background thread. `fallbackEncoding` as for Document::read. A file whose
// bytes aren't valid in its encoding (saving would alter them) or that can't hold the new text is left alone.
Outcome applyToFile(const QString &path, const Change &change, const QString &fallbackEncoding);

} // namespace NoteEdits
