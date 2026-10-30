#pragma once

#include "document/Document.h"

#include <QWidget>

class MarkdownEditor;

// One open file: the editor widget plus the file it's bound to.
class EditorTab : public QWidget {
    Q_OBJECT

public:
    explicit EditorTab(QWidget *parent = nullptr);

    MarkdownEditor *editor() const { return m_editor; }
    QString filePath() const { return m_document.filePath(); }
    QString displayName() const { return m_document.displayName(); }
    bool isUntitled() const { return m_document.isUntitled(); }
    bool isModified() const;
    // Untitled, unmodified and empty: opening a file may reuse this tab.
    bool isPristine() const;

    bool load(const QString &path, QString &error);
    bool save(const QString &path, QString &error);

signals:
    void stateChanged(); // file path or modified flag changed

private:
    MarkdownEditor *m_editor;
    Document m_document;
};
