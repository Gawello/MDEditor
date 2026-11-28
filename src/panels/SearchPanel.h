#pragma once

#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>

#include <atomic>
#include <memory>

class NoteIndex;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

// Search in the open folder (Ctrl+Shift+F): every text file, in the background, results grouped by file.
// A tag from the tag list shows the notes that have it.
class SearchPanel : public QWidget {
    Q_OBJECT

public:
    struct Match {
        int line = 0;
        int column = 0; // in the line, UTF-16
        int length = 0;
        QString text;   // the line
    };
    struct FileMatches {
        QString path;
        QList<Match> matches;
    };
    struct Query {
        QString text;
        bool caseSensitive = false;
        bool regex = false;
    };

    explicit SearchPanel(NoteIndex *index, QWidget *parent = nullptr);
    ~SearchPanel() override;

    void focusSearch(const QString &text = {}); // selects the field; with text, searches for it
    // Only the files in `folder` (the folder panel's Search in Folder); empty: the whole open folder.
    void setScope(const QString &folder);
    QString scope() const { return m_scope; }
    void showTag(const QString &tag);
    void search();
    bool searching() const { return m_watcher.isRunning(); }
    QTreeWidget *results() const { return m_results; }

    // The search itself (static, for the background thread and tests). Stops at `maxMatches`.
    static QList<FileMatches> run(const QStringList &files, const Query &query, const std::atomic<bool> *cancel);
    static constexpr int maxMatches = 2000;

signals:
    void openRequested(const QString &path, int line, int column, int length);

private:
    void showResults(const QList<FileMatches> &results, bool truncated);

    NoteIndex *m_index;
    QString m_scope;
    QWidget *m_scopeRow;
    QLabel *m_scopeLabel;
    QLineEdit *m_field;
    QCheckBox *m_case;
    QCheckBox *m_regex;
    QLabel *m_status;
    QTreeWidget *m_results;
    QFutureWatcher<QList<FileMatches>> m_watcher;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
