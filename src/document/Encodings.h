#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Character encodings of text files. Names are the ones QStringConverter takes ("UTF-8", "windows-1250"...);
// legacy encodings need Qt with ICU (Linux distributions) or the Windows code pages.
namespace Encodings {

inline const QString utf8 = QStringLiteral("UTF-8");

// What a file's bytes are, as far as can be told without asking.
struct Detection {
    QString encoding; // empty: binary
    bool bom = false;
    int bomLength = 0;
};

// Byte order mark first, then UTF-8 if the bytes are valid UTF-8, then UTF-16 without a BOM (NUL bytes in
// every other position), otherwise `fallback`. NUL bytes anywhere else mean a binary file.
Detection detect(const QByteArray &data, const QString &fallback);

// Encodings offered in menus, those this Qt can convert, in a sensible order.
QStringList common();
bool isSupported(const QString &encoding);
// The usual name for loosely written ones: "ISO8859-2" -> "ISO-8859-2", "microsoft-cp1251" -> "windows-1251".
QString normalized(const QString &encoding);
// For files that aren't UTF-8: the usual legacy encoding for the system's language (windows-1250 for
// Polish, Czech...), windows-1252 otherwise.
QString defaultFallback();
// Short label: "UTF-8", "UTF-8 with BOM", "Windows-1250".
QString label(const QString &encoding, bool bom);

// Decoding and encoding. `ok` is false when some bytes or characters had no equivalent.
QString decode(const QByteArray &data, const QString &encoding, bool *ok = nullptr);
QByteArray encode(const QString &text, const QString &encoding, bool bom, bool *ok = nullptr);

} // namespace Encodings
