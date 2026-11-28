#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QTextBrowser;
class QTimer;

// Obsidian's page preview: a linked note shown in a small window while Ctrl is held over the link. A
// tool-tip window (no focus, no input grab) that closes when the mouse leaves both it and the link.
class NotePreview : public QWidget {
    Q_OBJECT

public:
    explicit NotePreview(QWidget *editor);

    // `markdown` is already the excerpt (see MarkdownEditor::excerpt); `folder` resolves its images.
    void showNote(const QString &title, const QString &markdown, const QString &folder, const QPoint &globalPos,
                  const QRect &keepOpenOver);
    QString title() const;
    QTextBrowser *browser() const { return m_browser; }

private:
    void checkMouse();

    QLabel *m_title;
    QTextBrowser *m_browser;
    QTimer *m_watch;
    QRect m_keepOpenOver; // global: the link's line
};
