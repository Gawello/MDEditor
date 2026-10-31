#include "app/EditorTab.h"

#include "app/AppSettings.h"
#include "editor/MarkdownEditor.h"
#include "plugins/PluginManager.h"

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
    // Path first, so relative image paths resolve during the first highlight.
    m_editor->setDocumentPath(m_document.filePath());
    m_editor->setMarkdown(text);
    m_editor->document()->setModified(false);
    emit stateChanged();
    PluginManager::instance().notifyOpen(m_document.filePath());
    return true;
}

bool EditorTab::save(const QString &path, QString &error)
{
    if (AppSettings::instance().formatOnSave())
        m_editor->formatDocument(true);
    // Plugins may adjust the text before it's written (e.g. update a "modified" date).
    m_editor->replaceAll(PluginManager::instance().notifyBeforeSave(path, m_editor->markdown()));
    if (!m_document.save(path, m_editor->markdown(), error))
        return false;
    m_editor->document()->setModified(false);
    m_editor->setDocumentPath(m_document.filePath());
    emit stateChanged();
    PluginManager::instance().notifySave(m_document.filePath());
    return true;
}
