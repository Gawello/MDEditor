#pragma once

#include "editor/EditorTheme.h"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <functional>

// Syntax colors for a whole file of code (plain-text mode). Markdown files use LivePreviewHighlighter,
// which colors fenced code blocks with the same tokenizer (`applySyntax`).
class CodeHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    CodeHighlighter(QObject *parent, const EditorTheme *theme);

    // syntax:: language id; 0 is prose, which is spell checked instead. Null `document` detaches.
    void setLanguage(QTextDocument *document, int language);
    int language() const { return m_language; }
    // The cursor's position in the document, so the word being typed isn't marked.
    void setCaretProvider(std::function<int()> provider) { m_caret = std::move(provider); }

protected:
    void highlightBlock(const QString &text) override;

private:
    const EditorTheme *m_theme;
    int m_language = 0;
    std::function<int()> m_caret;
};

// Adds a red wavy underline to the misspelled words of `text` (block starting at `blockPosition`), keeping
// their other formatting; characters where `skip` is true aren't checked.
void underlineMisspelled(const QString &text, int blockPosition, int caret, const std::function<bool(int)> &skip,
                         const std::function<QTextCharFormat(int)> &formatAt,
                         const std::function<void(int, int, const QTextCharFormat &)> &setFormat);

// Applies syntax colors for `text` on top of `base` (font) through `setFormat`; returns the next state.
int applySyntax(int language, const QString &text, int state, const EditorTheme &theme, const QTextCharFormat &base,
                const std::function<void(int, int, const QTextCharFormat &)> &setFormat);
