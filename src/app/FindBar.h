#pragma once

#include <QFrame>
#include <QRegularExpression>
#include <QTextCursor>

class MarkdownEditor;
class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;

// VS Code-style find/replace bar shown above an editor: incremental search, match case / whole word /
// regex options, all matches highlighted, "n of m" counter, replace one or all (one undo step).
class FindBar : public QFrame {
    Q_OBJECT

public:
    explicit FindBar(MarkdownEditor *editor, QWidget *parent = nullptr);

    void openFind();
    void openReplace();
    void closeBar();

    bool findNext();
    bool findPrevious();
    bool replaceCurrent();
    int replaceAll();
    int matchCount() const { return m_matchCount; }

    // For tests and programmatic use.
    void setSearchText(const QString &text);
    void setReplaceText(const QString &text);
    void setOptions(bool matchCase, bool wholeWord, bool regex);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void open(bool replace);
    QRegularExpression pattern(QString *error = nullptr) const;
    QTextCursor findFrom(const QRegularExpression &regex, int position, bool backward) const;
    QString expandReplacement(const QString &matched) const;
    void refresh();         // highlights + counter
    void searchAsYouType(); // select the first match at or after the current selection

    MarkdownEditor *m_editor;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QToolButton *m_matchCase;
    QToolButton *m_wholeWord;
    QToolButton *m_regex;
    QToolButton *m_toggleReplace;
    QLabel *m_count;
    QWidget *m_replaceRow;
    QTimer *m_refreshTimer;
    int m_matchCount = 0;
};
