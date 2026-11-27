#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/EditorTab.h"
#include "document/Document.h"
#include "document/Encodings.h"
#include "editor/MarkdownEditor.h"

#include <QFile>
#include <QStringEncoder>
#include <QTemporaryDir>

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QByteArray in(const char *encoding, const QString &text)
{
    QStringEncoder encoder(encoding);
    return encoder(text);
}

const QString polish = QStringLiteral("Zażółć gęślą jaźń\nŁódź\n");

// Loads `data`, saves it unchanged (marked modified) and returns the bytes written.
QByteArray roundTrip(const QByteArray &data, const QString &fallback = QStringLiteral("windows-1250"))
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("file.txt"));
    writeFile(path, data);
    AppSettings::instance().setValue(AppSettings::fallbackEncodingKey, fallback);
    EditorTab tab;
    QString error;
    CHECK(tab.load(path, error));
    tab.editor()->document()->setModified(true);
    CHECK(tab.save(path, error) == EditorTab::SaveResult::Saved);
    return readFile(path);
}

} // namespace

TEST_CASE(encodings_are_detected)
{
    CHECK(Encodings::detect("plain ascii", QStringLiteral("windows-1250")).encoding == Encodings::utf8);
    CHECK(Encodings::detect(polish.toUtf8(), QStringLiteral("windows-1250")).encoding == Encodings::utf8);
    CHECK(Encodings::detect(in("windows-1250", polish), QStringLiteral("windows-1250")).encoding
          == QStringLiteral("windows-1250"));

    const auto bom = Encodings::detect(QByteArray("\xFF\xFEh\0i\0", 6), {});
    CHECK(bom.encoding == QStringLiteral("UTF-16LE") && bom.bom && bom.bomLength == 2);
    CHECK(Encodings::detect(in("UTF-16LE", QStringLiteral("hello world\n")), {}).encoding == QStringLiteral("UTF-16LE"));
    CHECK(Encodings::detect(in("UTF-16BE", QStringLiteral("hello world\n")), {}).encoding == QStringLiteral("UTF-16BE"));
    CHECK(Encodings::detect(QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR\x01\x02", 18), {}).encoding.isEmpty());
}

TEST_CASE(legacy_and_utf16_files_save_byte_for_byte)
{
    const QByteArray cp1250 = in("windows-1250", QString(polish).replace(u'\n', QStringLiteral("\r\n")));
    CHECK(roundTrip(cp1250) == cp1250);

    const QByteArray latin1 = in("ISO-8859-1", QStringLiteral("café déjà vu\n"));
    CHECK(roundTrip(latin1, QStringLiteral("windows-1252")) == latin1);

    QStringEncoder withBom("UTF-16LE", QStringConverter::Flag::WriteBom);
    const QByteArray utf16 = withBom(polish);
    CHECK(utf16.startsWith("\xFF\xFE"));
    CHECK(roundTrip(utf16) == utf16);

    const QByteArray utf8Bom = QByteArray("\xEF\xBB\xBF") + polish.toUtf8();
    CHECK(roundTrip(utf8Bom) == utf8Bom);
}

TEST_CASE(legacy_file_is_shown_correctly)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("old.txt"));
    writeFile(path, in("windows-1250", polish));
    AppSettings::instance().setValue(AppSettings::fallbackEncodingKey, QStringLiteral("windows-1250"));
    EditorTab tab;
    QString error;
    CHECK(tab.load(path, error));
    CHECK(tab.editor()->markdown() == polish);
    CHECK(tab.encoding() == QStringLiteral("windows-1250"));

    // Read the same bytes as Latin-2 on request: different letters, same file.
    CHECK(tab.reload(error, QStringLiteral("ISO-8859-2")));
    CHECK(tab.encoding() == QStringLiteral("ISO-8859-2"));
    CHECK(tab.editor()->markdown() != polish);
}

TEST_CASE(unencodable_text_is_not_saved)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("old.txt"));
    const QByteArray original = in("windows-1250", polish);
    writeFile(path, original);
    AppSettings::instance().setValue(AppSettings::fallbackEncodingKey, QStringLiteral("windows-1250"));
    EditorTab tab;
    QString error;
    CHECK(tab.load(path, error));
    tab.editor()->insertPlainText(QStringLiteral("→ ✓"));
    CHECK(tab.save(path, error) == EditorTab::SaveResult::Unencodable);
    CHECK(readFile(path) == original);

    tab.setEncoding(Encodings::utf8, false);
    CHECK(tab.save(path, error) == EditorTab::SaveResult::Saved);
    CHECK(QString::fromUtf8(readFile(path)).startsWith(QStringLiteral("→ ✓Zażółć")));
}

TEST_CASE(line_endings_can_be_changed)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("unix.txt"));
    writeFile(path, "a\nb\n");
    EditorTab tab;
    QString error;
    CHECK(tab.load(path, error));
    CHECK(!tab.usesCrlf());
    tab.setCrlf(true);
    CHECK(tab.isModified());
    CHECK(tab.save(path, error) == EditorTab::SaveResult::Saved);
    CHECK(readFile(path) == "a\r\nb\r\n");
}

#ifndef Q_OS_WIN
// English messages with Polish regional formats: old files are still Polish.
TEST_CASE(fallback_follows_regional_locale)
{
    const QByteArray lang = qgetenv("LANG");
    const QByteArray time = qgetenv("LC_TIME");
    qputenv("LANG", "en_US.UTF-8");
    qputenv("LC_TIME", "pl_PL.UTF-8");
    CHECK(Encodings::defaultFallback() == QStringLiteral("windows-1250"));
    qputenv("LANG", lang);
    qputenv("LC_TIME", time);
}
#endif
