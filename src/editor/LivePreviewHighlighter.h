#pragma once

#include "editor/EditorTheme.h"
#include "markdown/BlockParser.h"

#include <QSyntaxHighlighter>
#include <QTextBlockUserData>

#include <string_view>

inline std::u16string_view toView(const QString &text)
{
    return {reinterpret_cast<const char16_t *>(text.utf16()), size_t(text.size())};
}

// Parsed block structure, attached to every QTextBlock so the editor can paint
// decorations (quote bars, bullets, checkboxes, code backgrounds) without re-parsing.
class BlockData : public QTextBlockUserData {
public:
    md::BlockInfo info;
};

// Formats Markdown in place: syntax markers are hidden everywhere except in the
// block containing the cursor (the "active" block), where they are shown dimmed.
class LivePreviewHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    LivePreviewHighlighter(QTextDocument *document, const EditorTheme *theme);

    void setActiveBlock(int blockNumber);
    int activeBlock() const { return m_activeBlock; }

protected:
    void highlightBlock(const QString &text) override;

private:
    void applyInline(const QString &text, int offset, const QTextCharFormat &base, bool active);
    QTextCharFormat dimmed(QTextCharFormat format) const;
    QTextCharFormat invisible(QTextCharFormat format) const;
    QTextCharFormat collapsed(QTextCharFormat format) const;
    QTextCharFormat codeFormat() const;

    const EditorTheme *m_theme;
    int m_activeBlock = -1;
};
