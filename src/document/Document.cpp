#include "document/Document.h"

#include <QCoreApplication>
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
}

bool Document::load(const QString &path, QString &text, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();

    QStringDecoder decoder(QStringDecoder::Utf8); // strips a leading BOM
    text = decoder(data);
    if (decoder.hasError())
        text = QString::fromLocal8Bit(data);

    m_path = QFileInfo(path).absoluteFilePath();
    m_bom = data.startsWith(utf8Bom);
    m_crlf = data.contains("\r\n");
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return true;
}

bool Document::save(const QString &path, const QString &text, QString &error)
{
    QString contents = text;
    if (m_crlf)
        contents.replace(u'\n', QStringLiteral("\r\n"));

    QByteArray data = contents.toUtf8();
    if (m_bom)
        data.prepend(utf8Bom);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    return true;
}
