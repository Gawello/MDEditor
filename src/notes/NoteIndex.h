#pragma once

#include "markdown/NoteScan.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>

#include <memory>

// What the notes in the open folder contain (tags, links, headings, properties), and the list of all its
// files. Built in the background when the folder changes and refreshed when the app becomes active; files are
// read again only when their date or size changed. Nothing is indexed without an open folder.
class NoteIndex : public QObject {
    Q_OBJECT

public:
    struct Note {
        QString path; // absolute
        QDateTime modified;
        qint64 size = 0;
        md::NoteScan scan;
    };
    using Notes = QHash<QString, std::shared_ptr<const Note>>; // by path

    explicit NoteIndex(QObject *parent = nullptr);
    ~NoteIndex() override;

    void setRoot(const QString &root); // empty: no folder
    QString root() const { return m_root; }
    void refresh();                    // scans again in the background (no-op while a scan runs: queued)
    bool scanning() const { return m_watcher.isRunning(); }
    // An open tab changed or saved its note: use its text right away.
    void update(const QString &path, const QString &text);

    const Notes &notes() const { return m_notes; }
    const QStringList &files() const { return m_files; } // every file under the root, sorted
    QStringList noteNames() const;

    // The file a link points to, from the note `from`: [[Note]] by name anywhere in the folder (nearest first),
    // [[folder/Note]] and [text](relative/path.md) by path. Empty if it doesn't exist.
    QString resolve(const QString &target, bool wiki, const QString &from) const;
    // The `aliases:` of a note (also `alias:`), as written.
    QStringList aliases(const QString &path) const;
    static QStringList aliasesIn(const md::NoteScan &scan);
    // The line a link's anchor points to in `text`: a heading (`Heading`, `Parent#Heading`, its GitHub-style slug
    // `my-heading`, percent-encoded or not, any case) or a block (`^id`). -1 when there's none.
    static int findAnchor(const QString &text, const QString &anchor);
    // Tags with the number of notes using them, nested ones under their parents ("a/b" counts for "a" too).
    QHash<QString, int> tagCounts() const;

    static constexpr int maxFiles = 20000;
    static constexpr qint64 maxNoteSize = 4 * 1024 * 1024;

signals:
    void changed();

private:
    struct Result {
        QString root;
        QStringList files;
        Notes notes;
    };
    static Result scan(const QString &root, const Notes &previous);
    void finished();
    void indexAliases(); // rebuilds m_byAlias from the notes

    QString m_root;
    QStringList m_files;
    Notes m_notes;
    QHash<QString, QStringList> m_byName; // lower-case base name (no .md) -> paths
    QHash<QString, QStringList> m_byAlias; // lower-case alias -> paths
    QFutureWatcher<Result> m_watcher;
    bool m_again = false;
};
