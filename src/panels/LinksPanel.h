#pragma once

#include "notes/NoteIndex.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>

#include <atomic>
#include <memory>

class MarkdownEditor;
class QLabel;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// Backlinks of the current note (Obsidian's pane): the notes linking to it, each with the lines, and the
// unlinked mentions (its name or an alias in plain text elsewhere) with a Link button that turns one into a
// [[link]]. Worked out in the background, only while the panel is shown.
class BacklinksPanel : public QWidget {
    Q_OBJECT

public:
    struct Mention {
        QString source;
        int line = 0;
        int column = 0;
        int length = 0;
        QString text; // the whole line
    };
    struct Result {
        QString target;
        QList<Mention> linked;
        QList<Mention> unlinked;
    };

    explicit BacklinksPanel(NoteIndex *index, QWidget *parent = nullptr);
    ~BacklinksPanel() override;

    void setNote(const QString &path); // empty: nothing
    QString note() const { return m_note; }
    void refresh();                    // soon (the index changed)
    bool busy() const;
    QTreeWidget *tree() const { return m_tree; }

    // The background part (static, for tests): the lines of `links`, and `names` mentioned in `notes` outside of
    // links, code and tags (whole words, any case). Reads the files.
    static Result collect(const QString &target, const QList<NoteIndex::Backlink> &links, const QStringList &notes,
                          const QStringList &names, const std::atomic<bool> *cancel);
    static constexpr int maxMentions = 500;

signals:
    void openRequested(const QString &path, int line, int column, int length);
    // The Link button: replace the mention by a link to `target`.
    void linkMentionRequested(const BacklinksPanel::Mention &mention, const QString &target);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void start();
    void show(const Result &result);
    void addGroup(const QString &title, const QList<Mention> &mentions, bool linkButtons);

    NoteIndex *m_index;
    QString m_note;
    QTreeWidget *m_tree;
    QLabel *m_empty;
    QTimer *m_timer;
    QFutureWatcher<Result> m_watcher;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

// The links of the current note: to notes that exist and to notes not created yet. A click follows the link.
class OutgoingLinksPanel : public QWidget {
    Q_OBJECT

public:
    explicit OutgoingLinksPanel(NoteIndex *index, QWidget *parent = nullptr);

    void setEditor(MarkdownEditor *editor);
    void rebuild();
    QTreeWidget *tree() const { return m_tree; }

protected:
    void showEvent(QShowEvent *event) override;

private:
    NoteIndex *m_index;
    QPointer<MarkdownEditor> m_editor;
    QTreeWidget *m_tree;
    QLabel *m_empty;
    QTimer *m_timer;
    QList<QMetaObject::Connection> m_connections;
};
