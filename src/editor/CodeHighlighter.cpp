#include "editor/CodeHighlighter.h"

#include "editor/LivePreviewHighlighter.h" // toView
#include "spell/SpellChecker.h"

#include <QTextDocument>

int applySyntax(int language, const QString &text, int state, const EditorTheme &theme, const QTextCharFormat &base,
                const std::function<void(int, int, const QTextCharFormat &)> &setFormat)
{
    std::vector<syntax::Span> spans;
    const int next = syntax::highlightLine(language, toView(text), state, spans);
    for (const syntax::Span &span : spans) {
        const QColor color = theme.syntaxColor(span.token);
        if (!color.isValid())
            continue;
        QTextCharFormat format = base;
        format.setForeground(color);
        if (span.token == syntax::Token::Comment)
            format.setFontItalic(true);
        setFormat(span.start, span.length, format);
    }
    return next;
}

void underlineMisspelled(const QString &text, int blockPosition, int caret, const std::function<bool(int)> &skip,
                         const std::function<QTextCharFormat(int)> &formatAt,
                         const std::function<void(int, int, const QTextCharFormat &)> &setFormat)
{
    const int local = caret >= blockPosition && caret <= blockPosition + text.size() ? caret - blockPosition : -1;
    for (const auto &[start, end] : SpellChecker::instance().misspelled(text, skip, local)) {
        int i = start;
        while (i < end) { // per run of equal formats, so bold, links etc. stay
            QTextCharFormat format = formatAt(i);
            int j = i + 1;
            while (j < end && formatAt(j) == format)
                ++j;
            format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            format.setUnderlineColor(QColor(0xe0, 0x40, 0x40));
            setFormat(i, j - i, format);
            i = j;
        }
    }
}

CodeHighlighter::CodeHighlighter(QObject *parent, const EditorTheme *theme)
    : QSyntaxHighlighter(parent)
    , m_theme(theme)
{
}

void CodeHighlighter::setLanguage(QTextDocument *document, int language)
{
    if (language == m_language && document == this->document())
        return;
    m_language = language;
    setDocument(document); // highlights everything, or clears the formats
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    if (m_language == 0) {
        setCurrentBlockState(0);
        underlineMisspelled(
            text, currentBlock().position(), m_caret ? m_caret() : -1, [](int) { return false; },
            [this](int i) { return format(i); },
            [this](int start, int length, const QTextCharFormat &f) { setFormat(start, length, f); });
        return;
    }
    const int previous = previousBlockState();
    const int next = applySyntax(m_language, text, previous < 0 ? 0 : previous, *m_theme, QTextCharFormat(),
                                 [this](int start, int length, const QTextCharFormat &format) {
                                     setFormat(start, length, format);
                                 });
    setCurrentBlockState(next);
}
