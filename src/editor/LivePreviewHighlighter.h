#pragma once

#include "editor/EditorTheme.h"
#include "markdown/BlockParser.h"
#include "markdown/InlineParser.h"

#include <QColor>
#include <QFont>
#include <QSizeF>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBlockUserData>

#include <functional>
#include <string_view>
#include <vector>

inline std::u16string_view toView(const QString &text)
{
    return {reinterpret_cast<const char16_t *>(text.utf16()), size_t(text.size())};
}

// A character reference shown as what it stands for: the editor paints `text` over the space the
// highlighter reserved at `position`.
struct EntityGlyph {
    int position = 0;
    QString text;
    QFont font;
    QColor color; // invalid: the default text color
};

// A TeX formula shown in place of its source: inline at `position` (the slot left for it), or a whole
// display block.
struct MathGlyph {
    int position = 0;
    QString tex;
    bool display = false;
};

// Parsed block structure, attached to every QTextBlock so the editor can paint
// decorations (quote bars, bullets, checkboxes, code backgrounds, tables) without re-parsing.
class BlockData : public QTextBlockUserData {
public:
    md::BlockInfo info;
    QString image;       // target of the only thing on this line, if that's an image
    int imageWidth = 0;  // requested width (![[img.png|300]], ![alt|300](img.png)), 0 = natural
    bool imageShown = false; // the line is laid out to show the image instead of its text
    std::vector<EntityGlyph> entities;
    std::vector<MathGlyph> math; // inline formulas
    QString mathBlock;           // TeX of a display block drawn on this line ($$ ... $$)
    bool mathShown = false;      // the line is laid out to show `mathBlock`
    QString callout;             // the first line of a callout: its type, lower case
    // Folding belongs to the view, not the text: kept across re-highlighting, unlike everything above.
    bool folded = false;
    bool foldSeen = false; // a callout's "-" (folded by default) has been applied
};

// Vertical space above and below an image rendered in place of its line.
constexpr qreal imageVerticalPadding = 6;

// Display size for an image target (path relative to the document) and requested width;
// an empty size when the image can't be loaded.
using ImageSizeProvider = std::function<QSizeF(const QString &target, int requestedWidth)>;
// Size and baseline (from the top) of a rendered formula; an empty size when formulas can't be drawn.
struct MathMetrics {
    QSizeF size;
    qreal baseline = 0;
};
using MathSizeProvider = std::function<MathMetrics(const QString &tex, bool display)>;

md::BlockType blockType(const QTextBlock &block);
bool isTableBlock(const QTextBlock &block);
QTextBlock tableHeader(QTextBlock block); // first block of the table containing `block`
QTextBlock tableEnd(QTextBlock block);    // last block of the table containing `block`
// The lines of the $$ math block containing `block` (only meaningful when blockType(block) is a math type).
QTextBlock mathBlockStart(QTextBlock block);
QTextBlock mathBlockEnd(QTextBlock block);
QTextBlock frontmatterEnd(QTextBlock block); // the closing line of the frontmatter `block` is in
// The first line of the callout `block` belongs to, or an invalid block when it isn't in one.
QTextBlock calloutStart(QTextBlock block);

// Formats Markdown in place: syntax markers are hidden everywhere except in the
// block containing the cursor (the "active" block), where they are shown dimmed.
// A table is active as a whole while the cursor is anywhere inside it.
class LivePreviewHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    LivePreviewHighlighter(QTextDocument *document, const EditorTheme *theme);

    // Detaching clears every format and the parsed block data, leaving plain text; attaching
    // highlights the whole document again.
    void setAttached(bool attached);
    bool isAttached() const { return document() != nullptr; }

    // Returns true if anything had to be re-highlighted.
    bool setActiveBlock(int blockNumber);
    int activeBlock() const { return m_activeBlock; }

    void setImageSizeProvider(ImageSizeProvider provider) { m_imageSize = std::move(provider); }
    void setMathSizeProvider(MathSizeProvider provider) { m_mathSize = std::move(provider); }
    // The cursor's position in the document, so the word being typed isn't marked as misspelled.
    void setCaretProvider(std::function<int()> provider) { m_caret = std::move(provider); }

protected:
    void highlightBlock(const QString &text) override;

private:
    bool isActive(int blockNumber) const;
    void highlightTableRow(const QString &text, md::BlockType type, bool active);
    void applyInline(const QString &text, int offset, const QTextCharFormat &base, bool active);
    void showEntity(const md::EntityRange &entity, int offset);
    void recheckPreviousBlock(const QString &text);
    bool highlightImageLine(const QString &text, BlockData *data, bool active);
    void highlightMathBlock(const QString &text, const md::BlockInfo &info, BlockData *data, bool active);
    void showMath(const QString &text, const md::MathRange &math, int offset);
    void highlightFrontmatter(const QString &text, md::BlockType type, bool active);
    void highlightCallout(const QString &text, const md::BlockInfo &info, BlockData *data, bool active);
    QTextCharFormat tallSlot(qreal height) const;
    QTextCharFormat dimmed(QTextCharFormat format) const;
    QTextCharFormat invisible(QTextCharFormat format) const;
    QTextCharFormat collapsed(QTextCharFormat format) const;
    QTextCharFormat codeFormat() const;

    const EditorTheme *m_theme;
    QTextDocument *m_document; // also while detached
    ImageSizeProvider m_imageSize;
    MathSizeProvider m_mathSize;
    std::function<int()> m_caret;
    int m_activeBlock = -1;
    int m_tableFirst = -1; // block range of the table or math block containing the cursor, if any
    int m_tableLast = -1;
};
