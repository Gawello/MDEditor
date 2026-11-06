#include "document/Document.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>

namespace {

const QByteArray utf8Bom("\xEF\xBB\xBF");

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

void Document::reset()
{
    m_path.clear();
    m_crlf = false;
    m_bom = false;
    m_stamp = FileStamp();
}

QByteArray Document::encode(const QString &text) const
{
    QString contents = text;
    if (m_crlf)
        contents.replace(u'\n', QStringLiteral("\r\n"));

    QByteArray data = contents.toUtf8();
    if (m_bom)
        data.prepend(utf8Bom);
    return data;
}

void Document::adopt(const LoadedFile &file)
{
    m_path = file.path;
    m_crlf = file.crlf;
    m_bom = file.bom;
    m_stamp = file.stamp;
}

void Document::adopt(const SavedFile &file)
{
    m_path = file.path;
    m_stamp = file.stamp;
}

LoadedFile Document::read(const QString &path)
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

    QStringDecoder decoder(QStringDecoder::Utf8); // strips a leading BOM
    result.text = decoder(data);
    if (decoder.hasError())
        result.text = QString::fromLocal8Bit(data);

    result.path = QFileInfo(path).absoluteFilePath();
    result.bom = data.startsWith(utf8Bom);
    result.crlf = data.contains("\r\n");
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
