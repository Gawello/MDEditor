#include "app/Export.h"

#include "editor/MarkdownEditor.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUrl>

namespace Export {

namespace {

// Images get their width hint (![alt|300](pic.png)) and shrink to fit the page (QTextDocument draws them at
// their own size).
void fitImages(QTextDocument *document, const QString &folder, qreal width, const QHash<QString, int> &hints)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextImageFormat image = fragment.charFormat().toImageFormat();
            if (!image.isValid())
                continue;
            const QUrl url(image.name());
            const QString path = url.isLocalFile() ? url.toLocalFile()
                                 : QUrl(image.name()).isRelative() ? folder + u'/' + QUrl::fromPercentEncoding(image.name().toUtf8())
                                                                    : QString();
            const QSize size = path.isEmpty() ? QSize() : QImageReader(path).size();
            if (!size.isValid() || size.width() <= 0)
                continue;
            qreal wanted = hints.value(image.name(), size.width());
            if (width > 0)
                wanted = std::min(wanted, width);
            if (std::abs(wanted - size.width()) < 0.5)
                continue;
            image.setWidth(wanted);
            image.setHeight(wanted * size.height() / size.width());
            QTextCursor cursor(document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(image);
        }
    }
}

} // namespace

// What QTextDocument can't show: embedded notes go in as their text (one level), callouts become a quote with
// the title in bold.
static QString prepare(const QString &markdown, const Options &options)
{
    static const QRegularExpression embedLine(QStringLiteral(R"(^\s*!\[\[([^\]|]+)(?:\|[^\]]*)?\]\]\s*$)"));
    static const QRegularExpression callout(QStringLiteral(R"(^(\s*(?:>\s*)+)\[!([\w-]+)\][+-]?\s*(.*)$)"));
    QStringList lines = markdown.split(u'\n');
    QStringList result;
    for (const QString &line : std::as_const(lines)) {
        if (options.embed) {
            const QRegularExpressionMatch embedded = embedLine.match(line);
            const QString target = embedded.captured(1).trimmed();
            // A note: no extension, or .md (images keep theirs and are left to the image code).
            const QString file = target.section(u'#', 0, 0);
            if (embedded.hasMatch() && (QFileInfo(file).suffix().isEmpty() || MarkdownEditor::isMarkdownFile(file))) {
                const QString text = options.embed(target);
                if (!text.isEmpty()) {
                    result << QString() << text << QString();
                    continue;
                }
            }
        }
        const QRegularExpressionMatch box = callout.match(line);
        if (box.hasMatch()) {
            QString title = box.captured(3).trimmed();
            if (title.isEmpty()) {
                title = box.captured(2);
                title[0] = title[0].toUpper();
            }
            result << box.captured(1) + QStringLiteral("**") + title + QStringLiteral("**");
            continue;
        }
        result << line;
    }
    return result.join(u'\n');
}

QTextDocument *document(const QString &markdown, const Options &options)
{
    QString text = MarkdownEditor::excerpt(prepare(markdown, options), {}); // no front matter, [[links]] as text
    if (!options.title.isEmpty() && !text.trimmed().startsWith(QStringLiteral("# ")))
        text = QStringLiteral("# ") + options.title + QStringLiteral("\n\n") + text;
    auto *document = new QTextDocument;
    document->setDefaultFont(options.font);
    document->setBaseUrl(QUrl::fromLocalFile(options.folder + u'/'));
    document->setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    QHash<QString, int> hints; // image -> width asked for
    static const QRegularExpression sized(QStringLiteral(R"(!\[[^\]]*\|(\d+)(?:x\d+)?\]\(([^)\s]+))"));
    for (auto it = sized.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        hints.insert(match.captured(2), match.captured(1).toInt());
    }
    fitImages(document, options.folder, options.pageWidth, hints);
    return document;
}

bool toPdf(const QString &markdown, const Options &options, const QString &path, QString *error)
{
    QPdfWriter writer(path);
    writer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(20, 20, 20, 20),
                                     QPageLayout::Millimeter));
    writer.setResolution(96); // the document's pixels are screen pixels
    writer.setTitle(options.title);
    writer.setCreator(QStringLiteral("MDEditor"));
    Options fitted = options;
    fitted.pageWidth = writer.pageLayout().paintRectPixels(writer.resolution()).width();
    const std::unique_ptr<QTextDocument> document(Export::document(markdown, fitted));
    document->setPageSize(QSizeF(writer.pageLayout().paintRectPixels(writer.resolution()).size()));
    document->print(&writer);
    // QPdfWriter reports nothing: check what landed on disk.
    QFile written(path);
    if (!written.exists() || written.size() == 0) {
        if (error)
            *error = QObject::tr("The file couldn't be written.");
        return false;
    }
    return true;
}

bool toHtml(const QString &markdown, const Options &options, const QString &path, QString *error)
{
    const std::unique_ptr<QTextDocument> document(Export::document(markdown, options));
    QString html = document->toHtml();
    if (!options.title.isEmpty())
        html.replace(QStringLiteral("<head>"), QStringLiteral("<head><title>%1</title>").arg(options.title.toHtmlEscaped()));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(html.toUtf8()) < 0) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace Export
