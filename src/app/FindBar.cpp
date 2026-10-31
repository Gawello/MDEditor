#include "app/FindBar.h"

#include "editor/MarkdownEditor.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int maxHighlights = 2000;
constexpr int maxCounted = 10000;

QToolButton *toolButton(const QString &text, const QString &tip, bool checkable, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QString plainText(const QTextCursor &cursor)
{
    return cursor.selectedText().replace(QChar::ParagraphSeparator, u'\n');
}

} // namespace

FindBar::FindBar(MarkdownEditor *editor, QWidget *parent)
    : QFrame(parent)
    , m_editor(editor)
    , m_find(new QLineEdit(this))
    , m_replace(new QLineEdit(this))
    , m_matchCase(toolButton(QStringLiteral("Aa"), tr("Match case"), true, this))
    , m_wholeWord(toolButton(QStringLiteral("W"), tr("Match whole word"), true, this))
    , m_regex(toolButton(QStringLiteral(".*"), tr("Use regular expression"), true, this))
    , m_toggleReplace(toolButton(QStringLiteral("⇄"), tr("Toggle replace (Ctrl+H)"), true, this))
    , m_count(new QLabel(this))
    , m_replaceRow(new QWidget(this))
    , m_refreshTimer(new QTimer(this))
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setFrameShape(QFrame::NoFrame);
    hide();

    m_find->setPlaceholderText(tr("Find"));
    m_replace->setPlaceholderText(tr("Replace"));
    m_find->setClearButtonEnabled(true);
    m_find->installEventFilter(this);
    m_replace->installEventFilter(this);
    m_editor->installEventFilter(this);
    m_count->setMinimumWidth(80);
    m_count->setForegroundRole(QPalette::PlaceholderText);

    auto *previous = toolButton(QStringLiteral("↑"), tr("Previous match (Shift+F3)"), false, this);
    auto *next = toolButton(QStringLiteral("↓"), tr("Next match (F3)"), false, this);
    auto *close = toolButton(QStringLiteral("×"), tr("Close (Esc)"), false, this);
    connect(previous, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(next, &QToolButton::clicked, this, &FindBar::findNext);
    connect(close, &QToolButton::clicked, this, &FindBar::closeBar);
    connect(m_toggleReplace, &QToolButton::toggled, m_replaceRow, &QWidget::setVisible);

    auto *findRow = new QHBoxLayout;
    findRow->setContentsMargins(0, 0, 0, 0);
    findRow->addWidget(m_toggleReplace);
    findRow->addWidget(m_find, 1);
    findRow->addWidget(m_matchCase);
    findRow->addWidget(m_wholeWord);
    findRow->addWidget(m_regex);
    findRow->addWidget(m_count);
    findRow->addWidget(previous);
    findRow->addWidget(next);
    findRow->addWidget(close);

    auto *replaceOne = new QPushButton(tr("Replace"), m_replaceRow);
    auto *replaceEvery = new QPushButton(tr("Replace All"), m_replaceRow);
    replaceOne->setFocusPolicy(Qt::NoFocus);
    replaceEvery->setFocusPolicy(Qt::NoFocus);
    connect(replaceOne, &QPushButton::clicked, this, &FindBar::replaceCurrent);
    connect(replaceEvery, &QPushButton::clicked, this, &FindBar::replaceAll);
    auto *replaceRow = new QHBoxLayout(m_replaceRow);
    replaceRow->setContentsMargins(m_toggleReplace->sizeHint().width() + 6, 0, 0, 0);
    replaceRow->addWidget(m_replace, 1);
    replaceRow->addWidget(replaceOne);
    replaceRow->addWidget(replaceEvery);
    m_replaceRow->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    layout->addLayout(findRow);
    layout->addWidget(m_replaceRow);

    // Typing searches incrementally; document edits refresh highlights (debounced).
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(150);
    connect(m_refreshTimer, &QTimer::timeout, this, &FindBar::refresh);
    connect(m_find, &QLineEdit::textChanged, this, &FindBar::searchAsYouType);
    for (QToolButton *option : {m_matchCase, m_wholeWord, m_regex})
        connect(option, &QToolButton::toggled, this, &FindBar::searchAsYouType);
    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
        if (isVisible())
            m_refreshTimer->start();
    });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (isVisible())
            m_refreshTimer->start();
    });
}

void FindBar::openFind()
{
    open(false);
}

void FindBar::openReplace()
{
    open(true);
}

void FindBar::open(bool replace)
{
    // A single-line selection becomes the search text.
    const QString selected = plainText(m_editor->textCursor());
    if (!selected.isEmpty() && !selected.contains(u'\n')) {
        const QSignalBlocker blocker(m_find);
        m_find->setText(m_regex->isChecked() ? QRegularExpression::escape(selected) : selected);
    }
    if (replace)
        m_toggleReplace->setChecked(true);
    show();
    m_find->setFocus();
    m_find->selectAll();
    refresh();
}

void FindBar::closeBar()
{
    hide();
    m_editor->setExtraSelections({});
    m_editor->setFocus();
}

void FindBar::setSearchText(const QString &text)
{
    m_find->setText(text);
}

void FindBar::setReplaceText(const QString &text)
{
    m_replace->setText(text);
}

void FindBar::setOptions(bool matchCase, bool wholeWord, bool regex)
{
    m_matchCase->setChecked(matchCase);
    m_wholeWord->setChecked(wholeWord);
    m_regex->setChecked(regex);
}

QRegularExpression FindBar::pattern(QString *error) const
{
    const QString text = m_find->text();
    if (text.isEmpty())
        return {};
    QString source = m_regex->isChecked() ? text : QRegularExpression::escape(text);
    if (m_wholeWord->isChecked())
        source = QStringLiteral("\\b(?:%1)\\b").arg(source);
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (!m_matchCase->isChecked())
        options |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression regex(source, options);
    if (!regex.isValid() && error)
        *error = regex.errorString();
    return regex;
}

// Next (or previous) non-empty match from `position`; QTextDocument matches within single lines.
QTextCursor FindBar::findFrom(const QRegularExpression &regex, int position, bool backward) const
{
    const QTextDocument *document = m_editor->document();
    QTextDocument::FindFlags flags = backward ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    // QTextDocument::find overrides the pattern's case option with this flag.
    if (!(regex.patternOptions() & QRegularExpression::CaseInsensitiveOption))
        flags |= QTextDocument::FindCaseSensitively;
    while (position >= 0 && position < document->characterCount()) {
        const QTextCursor found = document->find(regex, position, flags);
        if (found.isNull() || found.hasSelection())
            return found;
        position = backward ? found.position() - 1 : found.position() + 1; // skip empty matches like ^
    }
    return {};
}

bool FindBar::findNext()
{
    const QRegularExpression regex = pattern();
    if (!regex.isValid() || regex.pattern().isEmpty())
        return false;
    QTextCursor found = findFrom(regex, m_editor->textCursor().selectionEnd(), false);
    if (found.isNull())
        found = findFrom(regex, 0, false); // wrap around
    if (found.isNull()) {
        refresh();
        return false;
    }
    m_editor->setTextCursor(found);
    m_editor->ensureCursorVisible();
    refresh();
    return true;
}

bool FindBar::findPrevious()
{
    const QRegularExpression regex = pattern();
    if (!regex.isValid() || regex.pattern().isEmpty())
        return false;
    QTextCursor found = findFrom(regex, m_editor->textCursor().selectionStart(), true);
    if (found.isNull())
        found = findFrom(regex, m_editor->document()->characterCount() - 1, true);
    if (found.isNull()) {
        refresh();
        return false;
    }
    m_editor->setTextCursor(found);
    m_editor->ensureCursorVisible();
    refresh();
    return true;
}

void FindBar::searchAsYouType()
{
    const QRegularExpression regex = pattern();
    if (regex.isValid() && !regex.pattern().isEmpty()) {
        QTextCursor found = findFrom(regex, m_editor->textCursor().selectionStart(), false);
        if (found.isNull())
            found = findFrom(regex, 0, false);
        if (!found.isNull()) {
            m_editor->setTextCursor(found);
            m_editor->ensureCursorVisible();
        }
    }
    refresh();
}

// Expands $1 / \1, $$, \n, \t and \\ in the replacement (regex mode only).
QString FindBar::expandReplacement(const QString &matched) const
{
    const QString replacement = m_replace->text();
    if (!m_regex->isChecked())
        return replacement;

    const QRegularExpression regex = pattern();
    const QRegularExpressionMatch match =
        QRegularExpression(QRegularExpression::anchoredPattern(regex.pattern()), regex.patternOptions())
            .match(matched);

    QString result;
    for (int i = 0; i < replacement.size(); ++i) {
        const QChar c = replacement[i];
        const QChar next = i + 1 < replacement.size() ? replacement[i + 1] : QChar();
        if ((c == u'$' || c == u'\\') && next.isDigit()) {
            int group = next.digitValue();
            ++i;
            if (i + 1 < replacement.size() && replacement[i + 1].isDigit()
                && group * 10 + replacement[i + 1].digitValue() <= match.lastCapturedIndex())
                group = group * 10 + replacement[++i].digitValue();
            result += match.hasMatch() ? match.captured(group) : (group == 0 ? matched : QString());
        } else if (c == u'$' && next == u'$') {
            result += u'$';
            ++i;
        } else if (c == u'\\' && next == u'n') {
            result += u'\n';
            ++i;
        } else if (c == u'\\' && next == u't') {
            result += u'\t';
            ++i;
        } else if (c == u'\\' && next == u'\\') {
            result += u'\\';
            ++i;
        } else {
            result += c;
        }
    }
    return result;
}

bool FindBar::replaceCurrent()
{
    const QRegularExpression regex = pattern();
    if (!regex.isValid() || regex.pattern().isEmpty())
        return false;

    // Like VS Code: the first press selects a match, the next one replaces it.
    QTextCursor cursor = m_editor->textCursor();
    const QString selected = plainText(cursor);
    const bool isMatch = cursor.hasSelection()
        && QRegularExpression(QRegularExpression::anchoredPattern(regex.pattern()), regex.patternOptions())
               .match(selected)
               .hasMatch();
    if (isMatch) {
        cursor.insertText(expandReplacement(selected));
        m_editor->setTextCursor(cursor);
    }
    return findNext() || isMatch;
}

int FindBar::replaceAll()
{
    const QRegularExpression regex = pattern();
    if (!regex.isValid() || regex.pattern().isEmpty())
        return 0;

    int replaced = 0;
    int position = 0;
    QTextCursor group(m_editor->document());
    group.beginEditBlock(); // one undo step
    while (true) {
        QTextCursor found = findFrom(regex, position, false);
        if (found.isNull())
            break;
        found.insertText(expandReplacement(plainText(found)));
        position = found.position();
        ++replaced;
    }
    group.endEditBlock();

    emit m_editor->statusMessage(tr("Replaced %n occurrence(s)", nullptr, replaced));
    refresh();
    return replaced;
}

void FindBar::refresh()
{
    QString error;
    const QRegularExpression regex = pattern(&error);
    QList<QTextEdit::ExtraSelection> selections;
    m_matchCount = 0;

    if (!error.isEmpty()) {
        m_count->setText(tr("Invalid regex"));
        m_count->setToolTip(error);
    } else if (regex.pattern().isEmpty()) {
        m_count->clear();
    } else {
        QColor other = palette().color(QPalette::Highlight);
        other.setAlphaF(0.3f);
        QColor current = palette().color(QPalette::Highlight);
        current.setAlphaF(0.65f);
        const QTextCursor cursor = m_editor->textCursor();

        int currentIndex = 0;
        int position = 0;
        while (m_matchCount < maxCounted) {
            const QTextCursor found = findFrom(regex, position, false);
            if (found.isNull())
                break;
            ++m_matchCount;
            const bool isCurrent =
                found.selectionStart() == cursor.selectionStart() && found.selectionEnd() == cursor.selectionEnd();
            if (isCurrent)
                currentIndex = m_matchCount;
            if (selections.size() < maxHighlights) {
                QTextEdit::ExtraSelection selection;
                selection.cursor = found;
                selection.format.setBackground(isCurrent ? current : other);
                selections << selection;
            }
            position = found.selectionEnd();
        }
        if (m_matchCount == 0)
            m_count->setText(tr("No results"));
        else if (currentIndex > 0)
            m_count->setText(tr("%1 of %2").arg(currentIndex).arg(m_matchCount));
        else
            m_count->setText(tr("%n match(es)", nullptr, m_matchCount));
        m_count->setToolTip({});
    }
    if (isVisible())
        m_editor->setExtraSelections(selections);
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (watched == m_editor) { // Esc in the editor closes an open bar; everything else passes through
            if (key->key() == Qt::Key_Escape && isVisible()) {
                closeBar();
                return true;
            }
            return false;
        }
        if (key->key() == Qt::Key_Escape) {
            closeBar();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (watched == m_replace) {
                if (key->modifiers() & Qt::ControlModifier)
                    replaceAll();
                else
                    replaceCurrent();
            } else if (key->modifiers() & Qt::ShiftModifier) {
                findPrevious();
            } else {
                findNext();
            }
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}
