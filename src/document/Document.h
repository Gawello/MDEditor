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
    bool binary = false; // not text: an image, an archive, a program...
    QString error;
    QString path;
    QString text;
    QString encoding; // as QStringConverter names it
    bool crlf = false;
    bool bom = false;
    bool lossy = false; // some bytes weren't valid in `encoding` (saving would change them)
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

// A text file on disk. Remembers the encoding details of the loaded file (character encoding, byte order
// mark, CRLF line endings) so that saving doesn't rewrite every line.
class Document {
public:
    QString filePath() const { return m_path; }
    bool isUntitled() const { return m_path.isEmpty(); }
    QString displayName() const;
    FileStamp stamp() const { return m_stamp; }
    void setStamp(const FileStamp &stamp) { m_stamp = stamp; }
    // The file was renamed or moved: same contents, new path.
    void setFilePath(const QString &path) { m_path = path; }

    QString encoding() const { return m_encoding; }
    bool hasBom() const { return m_bom; }
    bool usesCrlf() const { return m_crlf; }
    // Used by the next save.
    void setEncoding(const QString &encoding, bool bom);
    void setCrlf(bool crlf) { m_crlf = crlf; }

    void reset();
    // The bytes to write for `text`, with this document's encoding details. `ok` is false if some characters
    // can't be written in the encoding (they would turn into question marks or similar).
    QByteArray encode(const QString &text, bool *ok = nullptr) const;
    void adopt(const LoadedFile &file);
    void adopt(const SavedFile &file);

    // These touch the disk, which can take seconds or hang on a network drive, so they run off the GUI
    // thread. They use no Document state.
    // `fallback` is the encoding for files that aren't UTF-8; `forced`, if given, is used whatever the bytes.
    static LoadedFile read(const QString &path, const QString &fallback = {}, const QString &forced = {});
    static SavedFile write(const QString &path, const QByteArray &data);
    static DiskCheck check(const QString &path, const FileStamp &known);

private:
    QString m_path;
    QString m_encoding = QStringLiteral("UTF-8");
    bool m_crlf = false;
    bool m_bom = false;
    FileStamp m_stamp;
};
