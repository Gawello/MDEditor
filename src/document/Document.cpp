#include "document/Document.h"

#include "document/Encodings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Document", text);
}

FileStamp stampOf(const QString &path, const QByteArray &data)
{
    const QFileInfo info(path);
    FileStamp stamp;
    stamp.exists = true;
    stamp.size = info.size();
    stamp.modified = info.lastModified();
    stamp.hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return stamp;
}

} // namespace

QString Document::displayName() const
{
    return isUntitled() ? tr("Untitled") : QFileInfo(m_path).fileName();
}

void Document::setEncoding(const QString &encoding, bool bom)
{
    m_encoding = encoding;
    m_bom = bom;
}

void Document::reset()
{
    m_path.clear();
    m_encoding = Encodings::utf8;
    m_crlf = false;
    m_bom = false;
    m_stamp = FileStamp();
}

QByteArray Document::encode(const QString &text, bool *ok) const
{
    QString contents = text;
    if (m_crlf)
        contents.replace(u'\n', QStringLiteral("\r\n"));
    return Encodings::encode(contents, m_encoding, m_bom, ok);
}

void Document::adopt(const LoadedFile &file)
{
    m_path = file.path;
    m_encoding = file.encoding;
    m_crlf = file.crlf;
    m_bom = file.bom;
    m_stamp = file.stamp;
}

void Document::adopt(const SavedFile &file)
{
    m_path = file.path;
    m_stamp = file.stamp;
}

LoadedFile Document::read(const QString &path, const QString &fallback, const QString &forced)
{
    LoadedFile result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFile::NoError) {
        result.error = file.errorString(); // a dropped connection must not look like a short file
        return result;
    }

    Encodings::Detection detected = Encodings::detect(data, fallback.isEmpty() ? Encodings::defaultFallback() : fallback);
    if (!forced.isEmpty()) {
        // Keep a BOM that belongs to the forced encoding out of the text.
        const Encodings::Detection bom = Encodings::detect(data.left(4), {});
        const bool same = bom.bom && bom.encoding == forced;
        detected = {forced, same, same ? bom.bomLength : 0};
    }
    // Text never contains NUL bytes (outside UTF-16/32); nearly every binary format does, early on.
    if (detected.encoding.isEmpty()) {
        result.binary = true;
        result.error = tr("It is not a text file.");
        return result;
    }

    bool ok = true;
    result.text = Encodings::decode(data.mid(detected.bomLength), detected.encoding, &ok);
    result.lossy = !ok;
    result.encoding = detected.encoding;
    result.bom = detected.bom;
    result.path = QFileInfo(path).absoluteFilePath();
    result.crlf = result.text.contains(QStringLiteral("\r\n"));
    result.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    result.stamp = stampOf(path, data);
    result.ok = true;
    return result;
}

SavedFile Document::write(const QString &path, const QByteArray &data)
{
    SavedFile result;
    QSaveFile file(path);
    file.setDirectWriteFallback(true); // some mounts allow writing the file but not creating a temporary one
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        result.error = file.errorString();
        return result;
    }
    result.path = QFileInfo(path).absoluteFilePath();
    result.stamp = stampOf(path, data);
    result.ok = true;
    return result;
}

DiskCheck Document::check(const QString &path, const FileStamp &known)
{
    DiskCheck result;
    const QFileInfo info(path);
    if (!info.exists()) {
        result.state = known.exists ? DiskState::Missing : DiskState::Unchanged;
        result.stamp = known;
        return result;
    }
    // Same size and date: skip reading the file, which is the slow part on a network drive.
    if (known.exists && info.size() == known.size && info.lastModified() == known.modified) {
        result.state = DiskState::Unchanged;
        result.stamp = known;
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    const QByteArray data = file.readAll();
    if (file.error() != QFile::NoError)
        return result;
    result.stamp = stampOf(path, data);
    result.state = known.exists && result.stamp.hash == known.hash ? DiskState::Unchanged : DiskState::Changed;
    return result;
}
