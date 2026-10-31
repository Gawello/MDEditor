#include "editor/MarkdownEditor.h"

#include "app/AppSettings.h"
#include "editor/LivePreviewHighlighter.h"
#include "format/CodeFormatters.h"
#include "format/FormatterRegistry.h"
#include "markdown/InlineParser.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <numeric>

using md::BlockType;

namespace {

QStringList localFiles(const QMimeData *mime)
{
    QStringList files;
    if (!mime->hasUrls())
        return files;
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile())
            return {};
        files << url.toLocalFile();
    }
    return files;
}

bool isCode(BlockType type)
{
    return type == BlockType::FenceOpen || type == BlockType::FenceClose || type == BlockType::CodeLine;
}

bool isImageFile(const QString &path)
{
    static const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    return formats.contains(QFileInfo(path).suffix().toLower().toLatin1());
}

} // namespace

MarkdownEditor::MarkdownEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    applyFontSettings();

    setFrameShape(QFrame::NoFrame);
    // Fill the side margins (outside of the viewport) with the text background.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    viewport()->setMouseTracking(true);
    document()->setDocumentMargin(16);

    m_theme = EditorTheme::fromPalette(palette(), font());
    m_highlighter = new LivePreviewHighlighter(document(), &m_theme);
    m_highlighter->setImageSizeProvider(
        [this](const QString &target, int requestedWidth) { return imageDisplaySize(target, requestedWidth); });

    // Image sizes depend on the text column width: re-layout once resizing settles.
    m_imageRelayout = new QTimer(this);
    m_imageRelayout->setSingleShot(true);
    m_imageRelayout->setInterval(120);
    connect(m_imageRelayout, &QTimer::timeout, m_highlighter, &QSyntaxHighlighter::rehighlight);

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &MarkdownEditor::refreshActiveBlock);

    applyTheme();
    document()->setModified(false); // setDocumentMargin() counts as a modification

    connect(&AppSettings::instance(), &AppSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(AppSettings::fontFamilyKey) || key == QLatin1String(AppSettings::fontSizeKey))
            applyFontSettings();
        else if (key == QLatin1String(AppSettings::readableWidthKey) || key == QLatin1String(AppSettings::indentSizeKey))
            applyTheme();
    });
}

void MarkdownEditor::applyFontSettings()
{
    QFont base = QApplication::font();
    const QString family = AppSettings::instance().fontFamily();
    if (!family.isEmpty())
        base.setFamily(family);
    base.setPointSizeF(AppSettings::instance().fontSize());
    setFont(base); // FontChange re-applies the theme
}

QString MarkdownEditor::markdown() const
{
    // toPlainText() would also turn non-breaking spaces into plain spaces.
    QString text = document()->toRawText();
    text.replace(QChar::ParagraphSeparator, u'\n');
    text.replace(QChar::LineSeparator, u'\n');
    return text;
}

void MarkdownEditor::setMarkdown(const QString &text)
{
    setPlainText(text);
    refreshActiveBlock();
}

void MarkdownEditor::replaceAll(const QString &text)
{
    if (text == markdown())
        return;
    const int position = textCursor().position();
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.setPosition(std::min(position, document()->characterCount() - 1));
    setTextCursor(cursor);
}

void MarkdownEditor::refreshActiveBlock()
{
    if (m_highlighter->setActiveBlock(textCursor().blockNumber()))
        viewport()->update();
}

void MarkdownEditor::toggleWrap(const QString &marker)
{
    const int m = int(marker.size());
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();

    if (!cursor.hasSelection()) {
        const int pos = cursor.position();
        cursor.insertText(marker + marker);
        cursor.setPosition(pos + m);
    } else {
        const int start = cursor.selectionStart();
        const int end = cursor.selectionEnd();
        auto textAt = [this](int from, int length) {
            QTextCursor c(document());
            c.setPosition(from);
            c.setPosition(from + length, QTextCursor::KeepAnchor);
            return c.selectedText();
        };
        QTextCursor edit(document());
        if (start >= m && end + m <= document()->characterCount() - 1 && textAt(start - m, m) == marker
            && textAt(end, m) == marker) {
            edit.setPosition(end);
            edit.setPosition(end + m, QTextCursor::KeepAnchor);
            edit.removeSelectedText();
            edit.setPosition(start - m);
            edit.setPosition(start, QTextCursor::KeepAnchor);
            edit.removeSelectedText();
            cursor.setPosition(start - m);
            cursor.setPosition(end - m, QTextCursor::KeepAnchor);
        } else {
            edit.setPosition(end);
            edit.insertText(marker);
            edit.setPosition(start);
            edit.insertText(marker);
            cursor.setPosition(start + m);
            cursor.setPosition(end + m, QTextCursor::KeepAnchor);
        }
    }

    cursor.endEditBlock();
    setTextCursor(cursor);
}

bool MarkdownEditor::event(QEvent *event)
{
    // Wheel events over the side margins don't reach the viewport; scroll anyway.
    if (event->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent *>(event));
        return true;
    }
    // Leave Ctrl+Tab / Ctrl+Shift+Tab to the window's tab switching shortcuts.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->modifiers() & Qt::ControlModifier) && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)) {
            event->ignore();
            return false;
        }
    }
    return QPlainTextEdit::event(event);
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event)
{
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();

    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier
        && (tableEnter() || continueList()))
        return;
    if (key == Qt::Key_Tab && modifiers == Qt::NoModifier && (moveToTableCell(false) || indentListItem(false)))
        return;
    if (key == Qt::Key_Backtab && (moveToTableCell(true) || indentListItem(true)))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

void MarkdownEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        if (event->modifiers() & Qt::ControlModifier) {
            if (const auto target = linkTargetAt(pos)) {
                openLink(*target);
                return;
            }
        } else if (toggleTaskAt(pos) || focusTableCellAt(pos)) {
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseMoveEvent(event);
    const bool overLink = (event->modifiers() & Qt::ControlModifier)
        && linkTargetAt(event->position().toPoint()).has_value();
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void MarkdownEditor::paintEvent(QPaintEvent *event)
{
    const QRect area = event->rect();
    const QPointF offset = contentOffset();
    const qreal margin = document()->documentMargin();
    const int activeBlock = textCursor().blockNumber();

    // Backgrounds go below the text.
    {
        QPainter painter(viewport());
        for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
            const QRectF r = blockBoundingGeometry(block).translated(offset);
            if (r.top() > area.bottom())
                break;
            const auto *data = static_cast<BlockData *>(block.userData());
            if (data && block.isVisible() && isCode(data->info.type)) {
                painter.fillRect(QRectF(offset.x() + margin - 6, r.top(), viewport()->width() - 2 * margin + 12,
                                        r.height()),
                                 m_theme.codeBackground);
            }
        }
    }

    QPlainTextEdit::paintEvent(event);

    // Decorations replacing hidden markers go on top.
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    const QTextBlock cursorBlock = textCursor().block();
    const int cursorTable = isTableBlock(cursorBlock) ? tableHeader(cursorBlock).blockNumber() : -1;
    int skipUntil = -1; // last block of a table that has already been handled
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF r = blockBoundingGeometry(block).translated(offset);
        if (r.top() > area.bottom())
            break;
        const auto *data = static_cast<BlockData *>(block.userData());
        if (!data || !block.isVisible() || block.blockNumber() <= skipUntil)
            continue;
        if (md::isTableType(data->info.type)) {
            const QTextBlock header = tableHeader(block);
            if (header.blockNumber() != cursorTable)
                paintTable(painter, tableGeometry(header));
            skipUntil = tableEnd(block).blockNumber();
            continue;
        }
        if (block.blockNumber() == activeBlock)
            continue;
        const md::BlockInfo &info = data->info;

        switch (info.type) {
        case BlockType::Quote: {
            const QRectF c = characterRect(block, info.markerStart);
            painter.fillRect(QRectF(c.left(), r.top(), 3, r.height()), m_theme.quoteBar);
            break;
        }
        case BlockType::BulletItem: {
            const QRectF c = characterRect(block, info.markerStart);
            const qreal radius = std::max<qreal>(2.0, c.height() * 0.11);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_theme.faint);
            painter.drawEllipse(c.center(), radius, radius);
            break;
        }
        case BlockType::TaskItem: {
            const QRectF first = characterRect(block, info.checkboxStart);
            const QRectF last = characterRect(block, info.checkboxStart + 2);
            const qreal side = std::min(first.height() * 0.7, last.right() - first.left());
            QRectF box(0, 0, side, side);
            box.moveCenter(QPointF((first.left() + last.right()) / 2, first.center().y()));

            const QColor color = info.checked ? m_theme.accent : m_theme.faint;
            painter.setPen(QPen(color, 1.5));
            painter.setBrush(info.checked ? QBrush(color) : QBrush(Qt::NoBrush));
            painter.drawRoundedRect(box, 3, 3);
            if (info.checked) {
                QPainterPath tick;
                tick.moveTo(box.left() + side * 0.25, box.top() + side * 0.52);
                tick.lineTo(box.left() + side * 0.43, box.top() + side * 0.70);
                tick.lineTo(box.left() + side * 0.76, box.top() + side * 0.32);
                painter.setPen(QPen(palette().color(QPalette::Base), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(tick);
            }
            break;
        }
        case BlockType::Paragraph:
            if (data->imageShown)
                paintImage(painter, block, data->image, data->imageWidth);
            break;
        case BlockType::HorizontalRule: {
            const qreal y = r.center().y();
            painter.setPen(QPen(m_theme.rule, 1));
            painter.drawLine(QPointF(offset.x() + margin, y), QPointF(offset.x() + viewport()->width() - margin, y));
            break;
        }
        default:
            break;
        }
    }
}

void MarkdownEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateMargins();
    if (m_hasImages && m_imageRelayout && viewport()->width() != m_layoutWidth) {
        m_layoutWidth = viewport()->width();
        m_imageRelayout->start();
    }
}

void MarkdownEditor::setDocumentPath(const QString &filePath)
{
    m_documentPath = filePath;
    setBaseDirectory(QFileInfo(filePath).absolutePath());
}

void MarkdownEditor::setBaseDirectory(const QString &directory)
{
    if (directory == m_baseDirectory)
        return;
    m_baseDirectory = directory;
    if (!document()->isEmpty()) // relative image paths now resolve differently
        m_highlighter->rehighlight();
}

void MarkdownEditor::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange)
        applyTheme();
}

void MarkdownEditor::dragEnterEvent(QDragEnterEvent *event)
{
    if (!localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QPlainTextEdit::dragEnterEvent(event);
}

void MarkdownEditor::dragMoveEvent(QDragMoveEvent *event)
{
    if (!localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QPlainTextEdit::dragMoveEvent(event);
}

// Dropped images are linked into the text; other files are opened.
void MarkdownEditor::dropEvent(QDropEvent *event)
{
    const QStringList files = localFiles(event->mimeData());
    if (!files.isEmpty()) {
        event->acceptProposedAction();
        QStringList images;
        QStringList documents;
        for (const QString &file : files)
            (isImageFile(file) ? images : documents) << file;
        if (!images.isEmpty())
            insertImageLinks(images, cursorForPosition(event->position().toPoint()));
        if (!documents.isEmpty())
            emit filesDropped(documents);
        return;
    }
    QPlainTextEdit::dropEvent(event);
}

bool MarkdownEditor::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || QPlainTextEdit::canInsertFromMimeData(source);
}

// Pasting copied image files links them; pasting image data (a screenshot, "Copy image")
// stores it next to the note. Text wins when the clipboard has both (e.g. copied from Word).
void MarkdownEditor::insertFromMimeData(const QMimeData *source)
{
    const QStringList files = localFiles(source);
    if (!files.isEmpty() && std::all_of(files.begin(), files.end(), isImageFile)) {
        insertImageLinks(files, textCursor());
        return;
    }
    if (source->hasImage() && !source->hasText()) {
        pasteImage(qvariant_cast<QImage>(source->imageData()));
        return;
    }
    QPlainTextEdit::insertFromMimeData(source);
}

void MarkdownEditor::pasteImage(const QImage &image)
{
    if (image.isNull())
        return;
    const QString attachmentsFolder = AppSettings::instance().attachmentsFolder();
    if (m_documentPath.isEmpty()) {
        QMessageBox::information(this, tr("Paste Image"),
                                 tr("Save the note first: pasted images are stored in an \"%1\" folder next to it.")
                                     .arg(attachmentsFolder));
        return;
    }

    QDir directory(QFileInfo(m_documentPath).absolutePath());
    if (!directory.mkpath(attachmentsFolder) || !directory.cd(attachmentsFolder)) {
        QMessageBox::warning(this, tr("Paste Image"),
                             tr("Cannot create the folder %1.")
                                 .arg(QDir::toNativeSeparators(directory.filePath(attachmentsFolder))));
        return;
    }

    const QString stem = QStringLiteral("pasted-image-%1")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString name = stem + QStringLiteral(".png");
    for (int n = 1; directory.exists(name); ++n)
        name = QStringLiteral("%1-%2.png").arg(stem).arg(n);

    const QString path = directory.filePath(name);
    if (!image.save(path, "PNG")) {
        QMessageBox::warning(this, tr("Paste Image"),
                             tr("Cannot save the image to %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    insertImageLinks({path}, textCursor());
}

// Inserts one image link per line after the cursor's line.
void MarkdownEditor::insertImageLinks(const QStringList &files, QTextCursor cursor)
{
    cursor.clearSelection();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::EndOfBlock);
    for (const QString &file : files) {
        QString path = m_baseDirectory.isEmpty() ? file : QDir(m_baseDirectory).relativeFilePath(file);
        path.replace(u' ', QStringLiteral("%20"));
        if (!cursor.block().text().trimmed().isEmpty())
            cursor.insertBlock();
        cursor.insertText(QStringLiteral("![](%1)").arg(path));
    }
    cursor.insertBlock(); // move on, so the image renders right away
    cursor.endEditBlock();
    setTextCursor(cursor);
    setFocus();
}

QString MarkdownEditor::resolvePath(const QString &target) const
{
    const QString path = QUrl::fromPercentEncoding(target.section(u'#', 0, 0).toUtf8());
    if (path.isEmpty())
        return {};
    const QDir base(m_baseDirectory.isEmpty() ? QDir::currentPath() : m_baseDirectory);
    return QFileInfo(base.filePath(path)).absoluteFilePath();
}

// Loads (and caches) a local image; reloads it when the file changes. Remote images aren't fetched.
MarkdownEditor::CachedImage *MarkdownEditor::cachedImage(const QString &target) const
{
    if (QUrl(target).scheme().size() > 1)
        return nullptr;
    const QString path = resolvePath(target);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile()) {
        m_images.remove(path);
        return nullptr;
    }

    auto it = m_images.find(path);
    if (it == m_images.end() || it->modified != info.lastModified()) {
        QImageReader reader(path);
        reader.setAutoTransform(true); // honor EXIF rotation
        QImage image = reader.read();
        if (image.isNull()) {
            m_images.remove(path);
            return nullptr;
        }
        if (image.width() > 2400) // nobody needs full-resolution photos in a text column
            image = image.scaledToWidth(2400, Qt::SmoothTransformation);
        it = m_images.insert(path, {image, info.lastModified(), {}});
    }
    m_hasImages = true;
    return &it.value();
}

QSizeF MarkdownEditor::imageDisplaySize(const QString &target, int requestedWidth) const
{
    const CachedImage *cached = cachedImage(target);
    if (!cached)
        return {};
    const QSizeF natural = cached->image.size();
    const qreal available = viewport()->width() - 2 * document()->documentMargin();
    const qreal width = std::min<qreal>(requestedWidth > 0 ? requestedWidth : natural.width(), available);
    if (width <= 0 || natural.width() <= 0)
        return {};
    return {width, width * natural.height() / natural.width()};
}

void MarkdownEditor::paintImage(QPainter &painter, const QTextBlock &block, const QString &target,
                                int requestedWidth) const
{
    CachedImage *cached = cachedImage(target);
    const QSizeF size = imageDisplaySize(target, requestedWidth);
    if (!cached || size.isEmpty())
        return;

    const QRectF r = blockBoundingGeometry(block).translated(contentOffset());
    const QRectF area(contentOffset().x() + document()->documentMargin(), r.top() + imageVerticalPadding,
                      size.width(), size.height());
    const qreal dpr = viewport()->devicePixelRatioF();
    const QSize pixels = (size * dpr).toSize();
    if (cached->scaled.size() != pixels) {
        cached->scaled = QPixmap::fromImage(
            cached->image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        cached->scaled.setDevicePixelRatio(dpr);
    }

    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(area, 4, 4);
    painter.setClipPath(clip);
    painter.drawPixmap(area.topLeft(), cached->scaled);
    painter.restore();
}

void MarkdownEditor::applyTheme()
{
    if (m_applyingTheme || !m_highlighter) // setFont() in the constructor gets here first
        return;
    const QScopedValueRollback guard(m_applyingTheme, true);

    m_theme = EditorTheme::fromPalette(palette(), font());
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * AppSettings::instance().indentSize());
    m_highlighter->rehighlight();
    updateMargins();
    viewport()->update();
}

// Keeps lines at a comfortable reading width by centering the text column.
void MarkdownEditor::updateMargins()
{
    const int characters = AppSettings::instance().readableWidth();
    const int side = characters > 0 ? std::max(0, (width() - fontMetrics().averageCharWidth() * characters) / 2) : 0;
    if (viewportMargins().left() != side)
        setViewportMargins(side, 0, side, 0);
}

md::BlockInfo MarkdownEditor::blockInfo(const QTextBlock &block) const
{
    const auto state = md::BlockState::decode(block.previous().isValid() ? block.previous().userState() : -1);
    const QString nextText = block.next().isValid() ? block.next().text() : QString();
    const auto nextLine = block.next().isValid() ? std::optional(toView(nextText)) : std::nullopt;
    return md::parseLine(toView(block.text()), state, nextLine).info;
}

QRectF MarkdownEditor::characterRect(const QTextBlock &block, int position) const
{
    const QTextLayout *layout = block.layout();
    if (!layout)
        return {};
    const QTextLine line = layout->lineForTextPosition(position);
    if (!line.isValid())
        return {};
    const QPointF origin = blockBoundingGeometry(block).translated(contentOffset()).topLeft() + layout->position();
    const qreal x0 = line.cursorToX(position);
    const qreal x1 = line.cursorToX(position + 1);
    return QRectF(origin.x() + x0, origin.y() + line.y(), x1 - x0, line.height());
}

// Enter on a list item or quote starts the next item; Enter on an empty item ends the list.
bool MarkdownEditor::continueList()
{
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection())
        return false;

    const QTextBlock block = cursor.block();
    const md::BlockInfo info = blockInfo(block);
    if (!info.isList() && info.type != BlockType::Quote)
        return false;
    if (cursor.positionInBlock() < info.contentStart)
        return false;

    const QString text = block.text();
    cursor.beginEditBlock();
    if (text.mid(info.contentStart).trimmed().isEmpty()) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        QString prefix = text.left(info.indent);
        switch (info.type) {
        case BlockType::BulletItem:
            prefix += QChar(info.listMarker) + QStringLiteral(" ");
            break;
        case BlockType::TaskItem:
            prefix += QChar(info.listMarker) + QStringLiteral(" [ ] ");
            break;
        case BlockType::OrderedItem:
            prefix += QString::number(info.orderedNumber + 1) + QChar(info.listMarker) + QStringLiteral(" ");
            break;
        default:
            prefix = text.left(info.contentStart);
            break;
        }
        cursor.insertBlock();
        cursor.insertText(prefix);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
    return true;
}

bool MarkdownEditor::indentListItem(bool outdent)
{
    const QTextCursor cursor = textCursor();
    if (document()->findBlock(cursor.selectionStart()) != document()->findBlock(cursor.selectionEnd()))
        return false;

    const QTextBlock block = cursor.block();
    if (!blockInfo(block).isList())
        return false;

    QTextCursor edit(block);
    if (!outdent) {
        edit.insertText(QString(AppSettings::instance().indentSize(), u' '));
        return true;
    }

    const QString text = block.text();
    int remove = 0;
    if (text.startsWith(u'\t'))
        remove = 1;
    else
        while (remove < 4 && remove < text.size() && text[remove] == u' ')
            ++remove;
    if (remove > 0) {
        edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
        edit.removeSelectedText();
    }
    return true;
}

std::optional<QString> MarkdownEditor::linkTargetAt(const QPoint &pos) const
{
    const QTextCursor hit = cursorForPosition(pos);
    const QTextBlock block = hit.block();
    const md::BlockInfo info = blockInfo(block);
    if (isCode(info.type) || info.type == BlockType::Blank || info.type == BlockType::HorizontalRule)
        return std::nullopt;

    const int local = hit.positionInBlock() - info.contentStart;
    if (local < 0)
        return std::nullopt;

    const QString content = block.text().mid(info.contentStart);
    const md::InlineResult parsed = md::parseInline(toView(content));
    const md::LinkRange *link = md::linkAt(parsed, local);
    if (!link && local > 0)
        link = md::linkAt(parsed, local - 1);
    if (!link)
        return std::nullopt;
    return content.mid(link->urlStart, link->urlEnd - link->urlStart);
}

void MarkdownEditor::openLink(const QString &target)
{
    const QUrl url(target);
    if (url.isValid() && url.scheme().size() > 1) { // one-letter schemes are Windows drive letters
        QDesktopServices::openUrl(url);
        return;
    }

    const QString path = resolvePath(target);
    if (path.isEmpty())
        return;

    QFileInfo file(path);
    if (!file.exists() && file.suffix().isEmpty())
        file.setFile(file.filePath() + QStringLiteral(".md"));

    const QString suffix = file.suffix().toLower();
    if (suffix == u"md" || suffix == u"markdown" || suffix == u"txt")
        emit openFileRequested(file.absoluteFilePath());
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()));
}

// Clicking a rendered checkbox (outside of the line being edited) toggles it.
bool MarkdownEditor::toggleTaskAt(const QPoint &pos)
{
    const QTextBlock block = cursorForPosition(pos).block();
    if (block.blockNumber() == textCursor().blockNumber())
        return false;

    const md::BlockInfo info = blockInfo(block);
    if (info.type != BlockType::TaskItem)
        return false;

    const QRectF box = characterRect(block, info.checkboxStart).united(characterRect(block, info.checkboxStart + 2));
    if (!box.adjusted(-2, -2, 2, 2).contains(pos))
        return false;

    QTextCursor edit(document());
    edit.setPosition(block.position() + info.checkboxStart + 1);
    edit.setPosition(block.position() + info.checkboxStart + 2, QTextCursor::KeepAnchor);
    edit.insertText(info.checked ? QStringLiteral(" ") : QStringLiteral("x"));
    return true;
}

namespace {

constexpr qreal tableCellPadding = 10;
constexpr qreal tableCornerRadius = 6;

QString emptyTableRow(int columns)
{
    QString row = QStringLiteral("|");
    for (int c = 0; c < columns; ++c)
        row += QStringLiteral("     |");
    return row;
}

} // namespace

void MarkdownEditor::insertTable(int rows, int columns)
{
    rows = std::max(rows, 1);
    columns = std::max(columns, 1);

    QString delimiter = QStringLiteral("|");
    for (int c = 0; c < columns; ++c)
        delimiter += QStringLiteral(" --- |");
    QStringList lines{emptyTableRow(columns), delimiter};
    for (int r = 1; r < rows; ++r)
        lines << emptyTableRow(columns);

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.clearSelection();
    // Keep the table separated from surrounding text by blank lines.
    if (!cursor.block().text().trimmed().isEmpty()) {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
        cursor.insertBlock();
    } else if (cursor.block().previous().isValid() && !cursor.block().previous().text().trimmed().isEmpty()) {
        cursor.insertBlock();
    }
    const int start = cursor.position();
    cursor.insertText(lines.join(u'\n'));
    const QTextBlock next = cursor.block().next();
    if (next.isValid() && !next.text().trimmed().isEmpty())
        cursor.insertBlock();
    cursor.endEditBlock();

    cursor.setPosition(start + 2); // first header cell
    setTextCursor(cursor);
    setFocus();
}

MarkdownEditor::TableGeometry MarkdownEditor::tableGeometry(const QTextBlock &header) const
{
    TableGeometry table;
    const QTextBlock delimiter = header.next();
    if (blockType(delimiter) != BlockType::TableDelimiter)
        return table;
    table.aligns = md::parseDelimiterRow(toView(delimiter.text())).value_or(std::vector<md::Align>{});
    const int columns = int(table.aligns.size());
    if (columns == 0)
        return table;

    table.rows.push_back(header);
    for (QTextBlock b = delimiter.next(); b.isValid() && blockType(b) == BlockType::TableRow; b = b.next())
        table.rows.push_back(b);

    std::vector<qreal> widths(columns, 3 * tableCellPadding);
    for (size_t r = 0; r < table.rows.size(); ++r) {
        const QString text = table.rows[r].text();
        const md::TableRow row = md::splitTableRow(toView(text));
        for (int c = 0; c < columns && c < int(row.cells.size()); ++c) {
            const md::TableCell &cell = row.cells[c];
            qreal width = 0;
            for (const TextRun &run : cellRuns(text.mid(cell.start, cell.end - cell.start), r == 0))
                width += QFontMetricsF(run.font).horizontalAdvance(run.text);
            widths[c] = std::max(widths[c], width + 2 * tableCellPadding);
        }
    }

    // Shrink proportionally when the table is wider than the text column.
    const qreal margin = document()->documentMargin();
    const qreal available = viewport()->width() - 2 * margin;
    const qreal total = std::accumulate(widths.begin(), widths.end(), qreal(0));
    if (total > available && available > 0) {
        for (qreal &width : widths)
            width *= available / total;
    }

    table.edges.push_back(contentOffset().x() + margin);
    for (const qreal width : widths)
        table.edges.push_back(table.edges.back() + width);
    return table;
}

std::vector<MarkdownEditor::TextRun> MarkdownEditor::cellRuns(const QString &text, bool header) const
{
    std::vector<TextRun> runs;
    const md::InlineResult parsed = md::parseInline(toView(text));
    const int n = int(parsed.styles.size());
    int i = 0;
    while (i < n) {
        const std::uint16_t style = parsed.styles[i];
        int j = i + 1;
        while (j < n && parsed.styles[j] == style)
            ++j;
        if (!(style & md::Marker)) {
            TextRun run{text.mid(i, j - i), font(), m_theme.text, QColor()};
            if (header || (style & md::Bold))
                run.font.setBold(true);
            if (style & md::Italic)
                run.font.setItalic(true);
            if (style & md::Strike)
                run.font.setStrikeOut(true);
            if (style & md::Code) {
                run.font.setFamilies(m_theme.monoFamilies);
                run.color = m_theme.codeText;
                run.background = m_theme.codeBackground;
            }
            if (style & md::Link) {
                run.color = m_theme.accent;
                run.font.setUnderline(true);
            }
            if (style & md::Highlight)
                run.background = m_theme.highlightBackground;
            runs.push_back(run);
        }
        i = j;
    }
    return runs;
}

void MarkdownEditor::paintTable(QPainter &painter, const TableGeometry &table) const
{
    if (table.rows.empty() || table.edges.size() < 2)
        return;

    const QPointF offset = contentOffset();
    auto rectOf = [&](const QTextBlock &block) { return blockBoundingGeometry(block).translated(offset); };
    const qreal left = table.edges.front();
    const qreal right = table.edges.back();
    const QRectF headerRect = rectOf(table.rows.front());
    const qreal top = headerRect.top();
    const qreal bottom = rectOf(table.rows.back()).bottom();
    const QRectF outline(left, top, right - left, bottom - top);
    const QPen gridPen(m_theme.rule, 1);

    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(outline, tableCornerRadius, tableCornerRadius);
    painter.setClipPath(clip);
    painter.fillRect(QRectF(left, top, right - left, headerRect.height()),
                     EditorTheme::mix(palette().color(QPalette::Base), m_theme.text, 0.06));

    painter.setPen(gridPen);
    for (size_t r = 1; r < table.rows.size(); ++r) {
        const qreal y = rectOf(table.rows[r]).top();
        painter.drawLine(QPointF(left, y), QPointF(right, y));
    }
    for (size_t c = 1; c + 1 < table.edges.size(); ++c)
        painter.drawLine(QPointF(table.edges[c], top), QPointF(table.edges[c], bottom));

    const int columns = int(table.edges.size()) - 1;
    for (size_t r = 0; r < table.rows.size(); ++r) {
        const QTextBlock block = table.rows[r];
        const QRectF rowRect = rectOf(block);
        const QString text = block.text();
        const md::TableRow row = md::splitTableRow(toView(text));
        const qreal centerY = rowRect.center().y();

        for (int c = 0; c < columns && c < int(row.cells.size()); ++c) {
            const md::TableCell &cell = row.cells[c];
            const std::vector<TextRun> runs = cellRuns(text.mid(cell.start, cell.end - cell.start), r == 0);
            qreal width = 0;
            for (const TextRun &run : runs)
                width += QFontMetricsF(run.font).horizontalAdvance(run.text);

            const qreal cellLeft = table.edges[c];
            const qreal cellRight = table.edges[c + 1];
            qreal x = cellLeft + tableCellPadding;
            const md::Align align = c < int(table.aligns.size()) ? table.aligns[c] : md::Align::None;
            if (align == md::Align::Center)
                x = (cellLeft + cellRight - width) / 2;
            else if (align == md::Align::Right)
                x = cellRight - tableCellPadding - width;

            painter.save();
            painter.setClipRect(QRectF(cellLeft, rowRect.top(), cellRight - cellLeft, rowRect.height()),
                                Qt::IntersectClip);
            for (const TextRun &run : runs) {
                const QFontMetricsF metrics(run.font);
                const qreal runWidth = metrics.horizontalAdvance(run.text);
                if (run.background.isValid())
                    painter.fillRect(QRectF(x, centerY - metrics.height() / 2, runWidth, metrics.height()),
                                     run.background);
                painter.setFont(run.font);
                painter.setPen(run.color);
                painter.drawText(QPointF(x, centerY + (metrics.ascent() - metrics.descent()) / 2), run.text);
                x += runWidth;
            }
            painter.restore();
        }
    }
    painter.restore();

    painter.setPen(gridPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outline.adjusted(0.5, 0.5, -0.5, -0.5), tableCornerRadius, tableCornerRadius);
}

// Clicking a rendered table puts the cursor into the clicked cell (which reveals the Markdown).
bool MarkdownEditor::focusTableCellAt(const QPoint &pos)
{
    QTextBlock block = cursorForPosition(pos).block();
    if (!isTableBlock(block))
        return false;
    const QTextBlock header = tableHeader(block);
    if (isTableBlock(textCursor().block()) && tableHeader(textCursor().block()) == header)
        return false; // already editing this table: plain click

    const TableGeometry table = tableGeometry(header);
    if (table.edges.size() < 2 || pos.x() < table.edges.front() || pos.x() > table.edges.back())
        return false;
    if (blockType(block) == BlockType::TableDelimiter)
        block = header;

    int column = 0;
    while (column + 2 < int(table.edges.size()) && pos.x() >= table.edges[column + 1])
        ++column;
    placeInTableCell(block, column, false);
    return true;
}

void MarkdownEditor::placeInTableCell(const QTextBlock &block, int cell, bool select)
{
    const md::TableRow row = md::splitTableRow(toView(block.text()));
    QTextCursor cursor(document());
    if (row.cells.empty()) {
        cursor.setPosition(block.position() + block.length() - 1);
    } else {
        const md::TableCell &target = row.cells[std::clamp(cell, 0, int(row.cells.size()) - 1)];
        cursor.setPosition(block.position() + (select ? target.start : target.end));
        if (select)
            cursor.setPosition(block.position() + target.end, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
    ensureCursorVisible();
}

// Tab / Shift+Tab move between cells (selecting their content); Tab in the last cell adds a row.
bool MarkdownEditor::moveToTableCell(bool backwards)
{
    const QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const BlockType type = blockType(block);
    if (type != BlockType::TableHeader && type != BlockType::TableRow)
        return false;

    const md::TableRow row = md::splitTableRow(toView(block.text()));
    if (row.cells.empty())
        return false;
    const int pos = cursor.positionInBlock();
    int current = int(row.cells.size()) - 1;
    for (int i = 0; i < int(row.cells.size()); ++i) {
        if (pos <= row.cells[i].segmentEnd) {
            current = i;
            break;
        }
    }

    if (!backwards) {
        if (current + 1 < int(row.cells.size())) {
            placeInTableCell(block, current + 1, true);
            return true;
        }
        QTextBlock next = block.next();
        if (blockType(next) == BlockType::TableDelimiter)
            next = next.next();
        if (next.isValid() && blockType(next) == BlockType::TableRow)
            placeInTableCell(next, 0, true);
        else
            insertTableRowAfter(block);
        return true;
    }

    if (current > 0) {
        placeInTableCell(block, current - 1, true);
        return true;
    }
    QTextBlock previous = block.previous();
    if (blockType(previous) == BlockType::TableDelimiter)
        previous = previous.previous();
    if (isTableBlock(previous))
        placeInTableCell(previous, std::numeric_limits<int>::max(), true);
    return true;
}

// Enter adds a row below; Enter on an empty row leaves the table.
bool MarkdownEditor::tableEnter()
{
    QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const BlockType type = blockType(block);
    if (cursor.hasSelection() || (type != BlockType::TableHeader && type != BlockType::TableRow))
        return false;

    if (type == BlockType::TableRow) {
        const md::TableRow row = md::splitTableRow(toView(block.text()));
        const bool empty = std::all_of(row.cells.begin(), row.cells.end(),
                                       [](const md::TableCell &cell) { return cell.start == cell.end; });
        if (empty) {
            // Leave a blank line after the table: in GFM a following line without pipes would
            // otherwise still count as a table row.
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertBlock();
            cursor.endEditBlock();
            setTextCursor(cursor);
            return true;
        }
    }
    insertTableRowAfter(block);
    return true;
}

void MarkdownEditor::insertTableRowAfter(const QTextBlock &block)
{
    const QTextBlock header = tableHeader(block);
    const int columns = std::max<int>(1, int(md::splitTableRow(toView(header.text())).cells.size()));
    // A new row never goes between the header and the delimiter row.
    const QTextBlock after = blockType(block) == BlockType::TableHeader ? block.next() : block;

    QTextCursor cursor(after);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.beginEditBlock();
    cursor.insertBlock();
    cursor.insertText(emptyTableRow(columns));
    cursor.endEditBlock();
    placeInTableCell(after.next(), 0, false);
}

std::optional<MarkdownEditor::CodeBlock> MarkdownEditor::codeBlockAt(const QTextBlock &block) const
{
    if (!isCode(blockType(block)))
        return std::nullopt;
    QTextBlock open = block;
    if (blockType(open) == BlockType::FenceClose)
        open = open.previous();
    while (open.isValid() && blockType(open) != BlockType::FenceOpen)
        open = open.previous();
    if (!open.isValid())
        return std::nullopt;

    QTextBlock last = open;
    while (last.next().isValid() && blockType(last.next()) == BlockType::CodeLine)
        last = last.next();
    if (last == open)
        return std::nullopt; // empty block

    QString info = open.text().trimmed();
    while (info.startsWith(u'`') || info.startsWith(u'~'))
        info.remove(0, 1);
    CodeBlock code;
    code.start = open.next().position();
    code.end = last.position() + last.length() - 1;
    code.language = info.trimmed().section(u' ', 0, 0).toLower();
    return code;
}

QString MarkdownEditor::documentLanguage() const
{
    const QString suffix = QFileInfo(m_documentPath).suffix().toLower();
    if (suffix.isEmpty() || suffix == u"md" || suffix == u"markdown" || suffix == u"txt")
        return {};
    return FormatterRegistry::instance().languageForSuffix(suffix);
}

bool MarkdownEditor::formatRange(int start, int end, const QString &language, QString *message)
{
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, u'\n');

    const FormatterRegistry::Outcome outcome = FormatterRegistry::instance().format(language, text);
    if (!outcome.ok) {
        *message = tr("Cannot format %1: %2").arg(language, outcome.error);
        return false;
    }
    if (outcome.text != text)
        cursor.insertText(outcome.text);
    *message = tr("Formatted %1 (%2)").arg(language, outcome.formatter);
    return true;
}

bool MarkdownEditor::formatTable(const QTextBlock &header, QString *message)
{
    const QTextBlock last = tableEnd(header);
    std::vector<std::u16string> lines;
    for (QTextBlock b = header; b.isValid(); b = b.next()) {
        lines.push_back(b.text().toStdU16String());
        if (b == last)
            break;
    }
    QStringList formatted;
    for (const std::u16string &line : fmt::formatTable(lines))
        formatted << QString::fromStdU16String(line);

    QTextCursor cursor(document());
    cursor.setPosition(header.position());
    cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    const QString replacement = formatted.join(u'\n');
    if (cursor.selectedText().replace(QChar::ParagraphSeparator, u'\n') != replacement)
        cursor.insertText(replacement);
    *message = tr("Table aligned");
    return true;
}

void MarkdownEditor::formatAtCursor()
{
    const QTextCursor cursor = textCursor();
    QString message;

    if (cursor.hasSelection()) {
        QString language;
        if (const auto code = codeBlockAt(document()->findBlock(cursor.selectionStart())))
            language = code->language;
        if (language.isEmpty())
            language = documentLanguage();
        if (language.isEmpty()) {
            const QString text = cursor.selectedText().trimmed();
            if (text.startsWith(u'{') || text.startsWith(u'['))
                language = QStringLiteral("json");
            else if (text.startsWith(u'<'))
                language = QStringLiteral("xml");
        }
        if (language.isEmpty())
            message = tr("Can't tell the language of the selection: put it in a fenced code block like ```json");
        else
            formatRange(cursor.selectionStart(), cursor.selectionEnd(), language, &message);
    } else if (const auto code = codeBlockAt(cursor.block())) {
        if (code->language.isEmpty())
            message = tr("The code block has no language: add one after the fence, e.g. ```json");
        else
            formatRange(code->start, code->end, code->language, &message);
    } else if (isTableBlock(cursor.block())) {
        formatTable(tableHeader(cursor.block()), &message);
    } else if (!documentLanguage().isEmpty()) {
        formatDocument();
        return;
    } else {
        message = tr("Nothing to format here: select code, or put the cursor in a code block or table");
    }
    emit statusMessage(message);
}

int MarkdownEditor::formatDocument(bool quiet)
{
    QString message;
    const QString language = documentLanguage();
    if (!language.isEmpty()) {
        const bool ok = formatRange(0, document()->characterCount() - 1, language, &message);
        if (!quiet)
            emit statusMessage(message);
        return ok ? 1 : 0;
    }

    // Collect code blocks and tables first, then format bottom-up so positions stay valid.
    struct Target {
        int block;
        bool table;
    };
    std::vector<Target> targets;
    for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next()) {
        const BlockType type = blockType(b);
        if (type == BlockType::FenceOpen) {
            targets.push_back({b.blockNumber(), false});
        } else if (type == BlockType::TableHeader) {
            targets.push_back({b.blockNumber(), true});
            b = tableEnd(b);
        }
    }

    int formatted = 0;
    QString firstError;
    QTextCursor group(document());
    group.beginEditBlock(); // one undo step for the whole document
    for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
        const QTextBlock block = document()->findBlockByNumber(it->block);
        QString result;
        if (it->table) {
            formatted += formatTable(block, &result);
        } else if (const auto code = codeBlockAt(block.next()); code && !code->language.isEmpty()) {
            if (FormatterRegistry::instance().canonical(code->language).isEmpty())
                continue; // unknown languages are left alone
            if (formatRange(code->start, code->end, code->language, &result))
                ++formatted;
            else if (firstError.isEmpty())
                firstError = result;
        }
    }
    group.endEditBlock();

    if (!quiet) {
        message = firstError.isEmpty() ? tr("Formatted %n block(s)", nullptr, formatted)
                                       : tr("Formatted %n block(s); %1", nullptr, formatted).arg(firstError);
        emit statusMessage(message);
    }
    return formatted;
}
