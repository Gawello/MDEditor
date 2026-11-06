#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

// What a file looked like when it was last read or written, to notice changes made elsewhere: another
// computer, a sync client, another program. A full hash, because size and date alone can't tell a real
// change from a sync client rewriting the same bytes.
struct FileStamp {
    bool exists = false;
    qint64 size = 0;
    QDateTime modified;
    QByteArray hash;
};

struct LoadedFile {
    bool ok = false;
    QString error;
    QString path;
    QString text;
    bool crlf = false;
    bool bom = false;
    FileStamp stamp;
};

struct SavedFile {
    bool ok = false;
    QString error;
    QString path;
    FileStamp stamp;
};

enum class DiskState {
    Unchanged,
    Changed,
    Missing, // deleted, or the drive it's on is disconnected
    Unknown  // couldn't read it; saving will report the real error
};

struct DiskCheck {
    DiskState state = DiskState::Unknown;
    FileStamp stamp;
};

// A Markdown file on disk. Remembers the encoding details of the loaded file
// (UTF-8 BOM, CRLF line endings) so that saving doesn't rewrite every line.
class Document {
public:
    QString filePath() const { return m_path; }
    bool isUntitled() const { return m_path.isEmpty(); }
    QString displayName() const;
    FileStamp stamp() const { return m_stamp; }
    void setStamp(const FileStamp &stamp) { m_stamp = stamp; }

    void reset();
    // The bytes to write for `text`, with this document's encoding details.
    QByteArray encode(const QString &text) const;
    void adopt(const LoadedFile &file);
    void adopt(const SavedFile &file);

    // These touch the disk, which can take seconds or hang on a network drive, so they run off the GUI
    // thread. They use no Document state.
    static LoadedFile read(const QString &path);
    static SavedFile write(const QString &path, const QByteArray &data);
    static DiskCheck check(const QString &path, const FileStamp &known);

private:
    QString m_path;
    bool m_crlf = false;
    bool m_bom = false;
    FileStamp m_stamp;
};
