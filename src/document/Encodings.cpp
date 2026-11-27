#include "document/Encodings.h"

#include <algorithm>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // GetACP
#endif

#include <QCoreApplication>
#include <QLocale>
#include <QStringConverter>

namespace Encodings {

namespace {

struct Bom {
    QByteArray bytes;
    const char *encoding;
};

const Bom boms[] = {
    // UTF-32 first: its little-endian BOM starts with UTF-16's.
    {QByteArray("\xFF\xFE\x00\x00", 4), "UTF-32LE"},
    {QByteArray("\x00\x00\xFE\xFF", 4), "UTF-32BE"},
    {QByteArray("\xEF\xBB\xBF", 3), "UTF-8"},
    {QByteArray("\xFF\xFE", 2), "UTF-16LE"},
    {QByteArray("\xFE\xFF", 2), "UTF-16BE"},
};

// UTF-16 text in a Latin script has a NUL in every other byte; binary data has them scattered.
QString utf16WithoutBom(const QByteArray &data)
{
    const int length = int(std::min<qsizetype>(data.size(), 4096)) & ~1;
    if (length < 4)
        return {};
    int evenZeros = 0;
    int oddZeros = 0;
    for (int i = 0; i < length; i += 2) {
        evenZeros += data[i] == '\0';
        oddZeros += data[i + 1] == '\0';
    }
    const int pairs = length / 2;
    if (oddZeros * 10 >= pairs * 4 && evenZeros * 50 < pairs)
        return QStringLiteral("UTF-16LE");
    if (evenZeros * 10 >= pairs * 4 && oddZeros * 50 < pairs)
        return QStringLiteral("UTF-16BE");
    return {};
}

} // namespace

Detection detect(const QByteArray &data, const QString &fallback)
{
    for (const Bom &bom : boms) {
        if (data.startsWith(bom.bytes))
            return {QString::fromLatin1(bom.encoding), true, int(bom.bytes.size())};
    }
    if (data.left(64 * 1024).contains('\0')) {
        const QString utf16 = utf16WithoutBom(data);
        return {utf16, false, 0}; // empty: binary
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(data);
    Q_UNUSED(text);
    if (!decoder.hasError())
        return {utf8, false, 0};
    return {isSupported(fallback) ? fallback : QStringLiteral("ISO-8859-1"), false, 0};
}

bool isSupported(const QString &encoding)
{
    return QStringDecoder(encoding.toLatin1().constData()).isValid();
}

QStringList common()
{
    static const QStringList candidates{
        QStringLiteral("UTF-8"),        QStringLiteral("UTF-16LE"),     QStringLiteral("UTF-16BE"),
        QStringLiteral("windows-1250"), QStringLiteral("ISO-8859-2"),   QStringLiteral("IBM852"),
        QStringLiteral("windows-1252"), QStringLiteral("ISO-8859-1"),   QStringLiteral("ISO-8859-15"),
        QStringLiteral("windows-1251"), QStringLiteral("KOI8-R"),       QStringLiteral("ISO-8859-5"),
        QStringLiteral("windows-1253"), QStringLiteral("windows-1254"), QStringLiteral("windows-1257"),
        QStringLiteral("Shift_JIS"),    QStringLiteral("EUC-JP"),       QStringLiteral("GB18030"),
        QStringLiteral("Big5"),         QStringLiteral("EUC-KR"),
    };
    QStringList result;
    for (const QString &name : candidates) {
        if (isSupported(name))
            result << name;
    }
    return result;
}

namespace {

// The legacy (pre-Unicode) encoding used for a language, if it isn't windows-1252.
QString legacyEncoding(QLocale::Language language)
{
    switch (language) {
    case QLocale::Polish:
    case QLocale::Czech:
    case QLocale::Slovak:
    case QLocale::Hungarian:
    case QLocale::Slovenian:
    case QLocale::Croatian:
    case QLocale::Romanian:
    case QLocale::Bosnian:
        return QStringLiteral("windows-1250");
    case QLocale::Russian:
    case QLocale::Ukrainian:
    case QLocale::Belarusian:
    case QLocale::Bulgarian:
    case QLocale::Serbian:
    case QLocale::Macedonian:
        return QStringLiteral("windows-1251");
    case QLocale::Greek:
        return QStringLiteral("windows-1253");
    case QLocale::Turkish:
        return QStringLiteral("windows-1254");
    case QLocale::Lithuanian:
    case QLocale::Latvian:
    case QLocale::Estonian:
        return QStringLiteral("windows-1257");
    case QLocale::Japanese:
        return QStringLiteral("Shift_JIS");
    case QLocale::Chinese:
        return QStringLiteral("GB18030");
    case QLocale::Korean:
        return QStringLiteral("EUC-KR");
    default:
        return {};
    }
}

} // namespace

QString defaultFallback()
{
#ifdef Q_OS_WIN
    // Old files on Windows are in the system's ANSI code page.
    const QString codePage = QStringLiteral("windows-%1").arg(GetACP());
    if (isSupported(codePage))
        return codePage;
#endif
    // English messages with regional formats of another country are common (LANG=en_US, LC_TIME=pl_PL),
    // and QLocale::system() goes by the messages. Old files follow the country, so every locale counts.
    std::vector<QLocale::Language> languages{QLocale::system().language()};
    for (const char *variable : {"LC_ALL", "LC_CTYPE", "LC_TIME", "LC_NUMERIC", "LC_MONETARY", "LC_PAPER", "LANG"}) {
        const QString value = qEnvironmentVariable(variable).section(u'.', 0, 0);
        if (!value.isEmpty() && value != u"C" && value != u"POSIX")
            languages.push_back(QLocale(value).language());
    }
    for (const QLocale::Language language : languages) {
        const QString encoding = legacyEncoding(language);
        if (!encoding.isEmpty() && isSupported(encoding))
            return encoding;
    }
    return isSupported(QStringLiteral("windows-1252")) ? QStringLiteral("windows-1252") : QStringLiteral("ISO-8859-1");
}

QString label(const QString &encoding, bool bom)
{
    QString name = encoding;
    if (name.startsWith(u"windows-"))
        name[0] = u'W';
    if (name.startsWith(u"UTF-16") || name.startsWith(u"UTF-32"))
        name.insert(6, u' '); // "UTF-16 LE"
    return bom ? QCoreApplication::translate("Encodings", "%1 with BOM").arg(name) : name;
}

QString decode(const QByteArray &data, const QString &encoding, bool *ok)
{
    QStringDecoder decoder(encoding.toLatin1().constData(), QStringConverter::Flag::Stateless);
    if (!decoder.isValid()) {
        if (ok)
            *ok = false;
        return QString::fromLatin1(data);
    }
    QString text = decoder(data);
    if (ok)
        *ok = !decoder.hasError();
    return text;
}

QByteArray encode(const QString &text, const QString &encoding, bool bom, bool *ok)
{
    QStringEncoder encoder(encoding.toLatin1().constData(),
                           bom ? QStringConverter::Flag::WriteBom : QStringConverter::Flag::Default);
    if (!encoder.isValid()) {
        if (ok)
            *ok = false;
        return text.toUtf8();
    }
    QByteArray data = encoder(text);
    if (ok)
        *ok = !encoder.hasError();
    return data;
}

} // namespace Encodings
