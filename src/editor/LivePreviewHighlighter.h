#pragma once

#include "editor/EditorTheme.h"
#include "markdown/BlockParser.h"

#include <QSizeF>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBlockUserData>

#include <functional>
#include <string_view>

inline std::u16string_view toView(const QString &text)
{
    return {reinterpret_cast<const char16_t *>(text.utf16()), size_t(text.size())};
}

// Parsed block structure, attached to every QTextBlock so the editor can paint
// decorations (quote bars, bullets, checkboxes, code backgrounds, tables) without re-parsing.
class BlockData : public QTextBlockUserData {
public:
    md::BlockInfo info;
    QString image;       // target of the only thing on this line, if that's an image
    int imageWidth = 0;  // requested width (![[img.png|300]], ![alt|300](img.png)), 0 = natural
    bool imageShown = false; // the line is laid out to show the image instead of its text
};

// Vertical space above and below an image rendered in place of its line.
constexpr qreal imageVerticalPadding = 6;

// Display size for an image target (path relative to the document) and requested width;
// an empty size when the image can't be loaded.
using ImageSizeProvider = std::function<QSizeF(const QString &target, int requestedWidth)>;

md::BlockType blockType(const QTextBlock &block);
bool isTableBlock(const QTextBlock &block);
QTextBlock tableHeader(QTextBlock block); // first block of the table containing `block`
QTextBlock tableEnd(QTextBlock block);    // last block of the table containing `block`

// Formats Markdown in place: syntax markers are hidden everywhere except in the
// block containing the cursor (the "active" block), where they are shown dimmed.
// A table is active as a whole while the cursor is anywhere inside it.
class LivePreviewHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    LivePreviewHighlighter(QTextDocument *document, const EditorTheme *theme);

    // Returns true if anything had to be re-highlighted.
    bool setActiveBlock(int blockNumber);
    int activeBlock() const { return m_activeBlock; }

    void setImageSizeProvider(ImageSizeProvider provider) { m_imageSize = std::move(provider); }

protected:
    void highlightBlock(const QString &text) override;

private:
    bool isActive(int blockNumber) const;
    void highlightTableRow(const QString &text, md::BlockType type, bool active);
    void applyInline(const QString &text, int offset, const QTextCharFormat &base, bool active);
    void recheckPreviousBlock(const QString &text);
    bool highlightImageLine(const QString &text, BlockData *data, bool active);
    QTextCharFormat dimmed(QTextCharFormat format) const;
    QTextCharFormat invisible(QTextCharFormat format) const;
    QTextCharFormat collapsed(QTextCharFormat format) const;
    QTextCharFormat codeFormat() const;

    const EditorTheme *m_theme;
    ImageSizeProvider m_imageSize;
    int m_activeBlock = -1;
    int m_tableFirst = -1; // block range of the table containing the cursor, if any
    int m_tableLast = -1;
};
