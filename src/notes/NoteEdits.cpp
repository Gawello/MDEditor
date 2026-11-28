#include "notes/NoteEdits.h"

#include "document/Document.h"

#include <QCoreApplication>

namespace NoteEdits {

Outcome applyToFile(const QString &path, const Change &change, const QString &fallbackEncoding)
{
    Outcome outcome;
    outcome.path = path;
    const LoadedFile loaded = Document::read(path, fallbackEncoding);
    if (!loaded.ok || loaded.binary) {
        outcome.error = loaded.ok ? QCoreApplication::translate("NoteEdits", "not a text file") : loaded.error;
        return outcome;
    }
    if (loaded.lossy) {
        outcome.error = QCoreApplication::translate("NoteEdits", "some bytes aren't valid in its encoding (%1)")
                            .arg(loaded.encoding);
        return outcome;
    }
    const std::optional<QString> text = change(loaded.text);
    if (!text || *text == loaded.text)
        return outcome;
    Document document;
    document.adopt(loaded);
    bool encodable = true;
    const QByteArray data = document.encode(*text, &encodable);
    if (!encodable) {
        outcome.error = QCoreApplication::translate("NoteEdits", "the new text can't be written in %1").arg(loaded.encoding);
        return outcome;
    }
    const SavedFile saved = Document::write(path, data);
    if (!saved.ok) {
        outcome.error = saved.error;
        return outcome;
    }
    outcome.changed = true;
    outcome.text = *text;
    return outcome;
}

} // namespace NoteEdits
