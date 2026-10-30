#include "app/EditorTab.h"

#include "editor/MarkdownEditor.h"

#include <QFileInfo>
#include <QVBoxLayout>

EditorTab::EditorTab(QWidget *parent)
    : QWidget(parent)
    , m_editor(new MarkdownEditor(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);
    setFocusProxy(m_editor);

    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &EditorTab::stateChanged);
}

bool EditorTab::isModified() const
{
    return m_editor->document()->isModified();
}

bool EditorTab::isPristine() const
{
    return isUntitled() && !isModified() && m_editor->document()->isEmpty();
}

bool EditorTab::load(const QString &path, QString &error)
{
    Document document;
    QString text;
    if (!document.load(path, text, error))
        return false;

    m_document = document;
    m_editor->setMarkdown(text);
    m_editor->document()->setModified(false);
    m_editor->setBaseDirectory(QFileInfo(m_document.filePath()).absolutePath());
    emit stateChanged();
    return true;
}

bool EditorTab::save(const QString &path, QString &error)
{
    if (!m_document.save(path, m_editor->markdown(), error))
        return false;
    m_editor->document()->setModified(false);
    m_editor->setBaseDirectory(QFileInfo(m_document.filePath()).absolutePath());
    emit stateChanged();
    return true;
}
