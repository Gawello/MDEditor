#include "TestHarness.h"
#include "app/Export.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>

#include <memory>

namespace {

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

const QString note = QStringLiteral(
    "---\ntags: [x]\nsecret: yes\n---\nIntro with [[Other note|a link]] and **bold**.\n\n## Part\n\n- one\n- two\n\n"
    "| a | b |\n|---|---|\n| 1 | 2 |\n\n![](wide.png)\n\n```\ncode\n```\n");

} // namespace

TEST_CASE(exporting_notes)
{
    QTemporaryDir dir;
    QImage wide(3000, 300, QImage::Format_RGB32);
    wide.fill(Qt::darkGreen);
    CHECK(wide.save(dir.filePath(QStringLiteral("wide.png"))));
    Export::Options options{QStringLiteral("My Note"), dir.path(), QFont(), 600, {}};

    // The document: a title, no front matter, links as text, the wide image fitted to the page.
    std::unique_ptr<QTextDocument> document(Export::document(note, options));
    const QString text = document->toPlainText();
    CHECK(text.startsWith("My Note"));
    CHECK(!text.contains("secret") && text.contains("Intro with a link and bold."));
    bool fitted = false;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextImageFormat image = it.fragment().charFormat().toImageFormat();
            if (image.isValid())
                fitted = std::abs(image.width() - 600) < 1 && std::abs(image.height() - 60) < 1;
        }
    }
    CHECK(fitted);
    // A width hint, also from a [[wiki]] embed.
    std::unique_ptr<QTextDocument> hinted(Export::document(QStringLiteral("![[wide.png|300]]"), options));
    bool sized = false;
    for (QTextBlock block = hinted->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextImageFormat image = it.fragment().charFormat().toImageFormat();
            if (image.isValid())
                sized = std::abs(image.width() - 300) < 1;
        }
    }
    CHECK(sized);

    // Callouts as a quote with a bold title; embedded notes with their text.
    options.embed = [](const QString &target) {
        return target == u"Other#Part" ? QStringLiteral("## Part\nembedded words") : QString();
    };
    std::unique_ptr<QTextDocument> rich(Export::document(
        QStringLiteral("> [!tip] Handy\n> body\n\n> [!warning]-\n> x\n\n![[Other#Part]]\n\n![[Missing]]\n"), options));
    const QString richText = rich->toPlainText();
    CHECK(richText.contains("Handy") && !richText.contains("[!tip]") && richText.contains("Warning"));
    CHECK(richText.contains("embedded words") && richText.contains("Missing"));
    options.embed = {};

    // PDF and HTML files.
    const QString pdf = dir.filePath(QStringLiteral("out.pdf"));
    QString error;
    CHECK(Export::toPdf(note, options, pdf, &error));
    const QByteArray bytes = readBytes(pdf);
    CHECK(bytes.startsWith("%PDF") && bytes.size() > 2000);
    const QString html = dir.filePath(QStringLiteral("out.html"));
    CHECK(Export::toHtml(note, options, html, &error));
    const QString page = QString::fromUtf8(readBytes(html));
    CHECK(page.contains("<title>My Note</title>") && page.contains("<table") && page.contains("Part"));
    CHECK(!page.contains("secret"));
    // A folder that doesn't exist: an error, not a crash.
    CHECK(!Export::toHtml(note, options, dir.filePath(QStringLiteral("no/such/out.html")), &error) && !error.isEmpty());
}
