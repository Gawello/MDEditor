#include "app/EditorTab.h"

#include "app/AppSettings.h"
#include "app/Background.h"
#include "app/ExternalChangeBar.h"
#include "app/FindBar.h"
#include "editor/MarkdownEditor.h"
#include "plugins/PluginManager.h"

#include <QFileInfo>
#include <QVBoxLayout>

EditorTab::EditorTab(QWidget *parent)
    : QWidget(parent)
    , m_editor(new MarkdownEditor(this))
    , m_findBar(new FindBar(m_editor, this))
    , m_changeBar(new ExternalChangeBar(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_changeBar);
    layout->addWidget(m_findBar);
    layout->addWidget(m_editor);
    setFocusProxy(m_editor);

    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &EditorTab::stateChanged);
    connect(&m_diskCheck, &QFutureWatcher<DiskCheck>::finished, this, &EditorTab::handleDiskCheck);
    connect(m_changeBar, &ExternalChangeBar::reloadRequested, this, [this] {
        QString error;
        if (!reload(error))
            emit statusMessage(tr("Cannot reload %1: %2").arg(displayName(), error));
    });
    connect(m_changeBar, &ExternalChangeBar::keepRequested, this, &EditorTab::keepMyVersion);
}

bool EditorTab::isModified() const
{
    return m_editor->document()->isModified();
}

bool EditorTab::isPristine() const
{
    return isUntitled() && !isModified() && m_editor->document()->isEmpty();
}

QString EditorTab::waitLabel() const
{
    return tr("Waiting for %1…").arg(displayName());
}

bool EditorTab::load(const QString &path, QString &error)
{
    const auto file = runAndWait([path] { return Document::read(path); },
                                 tr("Waiting for %1…").arg(QFileInfo(path).fileName()), this);
    if (!file) {
        error = tr("Stopped waiting for the drive.");
        return false;
    }
    if (!file->ok) {
        error = file->error;
        return false;
    }

    m_document.adopt(*file);
    // Path first, so relative image paths resolve during the first highlight.
    m_editor->setDocumentPath(m_document.filePath());
    m_editor->setMarkdown(file->text);
    m_editor->document()->setModified(false);
    m_changeBar->hide();
    emit stateChanged();
    PluginManager::instance().notifyOpen(m_document.filePath());
    return true;
}

bool EditorTab::reload(QString &error)
{
    const QString path = filePath();
    const auto file = runAndWait([path] { return Document::read(path); }, waitLabel(), this);
    if (!file) {
        error = tr("Stopped waiting for the drive.");
        return false;
    }
    if (!file->ok) {
        error = file->error;
        return false;
    }

    m_document.adopt(*file);
    m_editor->replaceAll(file->text); // through the undo stack: the discarded text is one Ctrl+Z away
    m_editor->document()->setModified(false);
    m_changeBar->hide();
    emit stateChanged();
    return true;
}

EditorTab::SaveResult EditorTab::save(const QString &path, QString &error, bool overwrite)
{
    // Save As targets a different file, and the file dialog already asked about replacing it.
    if (!overwrite && !isUntitled() && QFileInfo(path).absoluteFilePath() == filePath()) {
        const FileStamp known = m_document.stamp();
        const auto check = runAndWait([path, known] { return Document::check(path, known); }, waitLabel(), this);
        if (!check) {
            error = tr("Stopped waiting for the drive. Nothing was saved.");
            return SaveResult::Failed;
        }
        if (check->state == DiskState::Changed || check->state == DiskState::Missing) {
            m_conflictState = check->state;
            return SaveResult::Conflict;
        }
    }

    if (AppSettings::instance().formatOnSave())
        m_editor->formatDocument(true);
    // Plugins may adjust the text before it's written (e.g. update a "modified" date).
    m_editor->replaceAll(PluginManager::instance().notifyBeforeSave(path, m_editor->markdown()));

    const QByteArray data = m_document.encode(m_editor->markdown());
    const auto file = runAndWait([path, data] { return Document::write(path, data); }, waitLabel(), this);
    if (!file) {
        // The write can't be cancelled, so the tab stays modified and the next save checks the disk again.
        error = tr("Stopped waiting for the drive. The file may still be written later: check it before relying on it.");
        return SaveResult::Failed;
    }
    if (!file->ok) {
        error = file->error;
        return SaveResult::Failed;
    }

    m_document.adopt(*file);
    m_editor->document()->setModified(false);
    m_editor->setDocumentPath(m_document.filePath());
    m_changeBar->hide();
    emit stateChanged();
    PluginManager::instance().notifySave(m_document.filePath());
    return SaveResult::Saved;
}

void EditorTab::checkDisk()
{
    if (isUntitled() || m_diskCheck.isRunning())
        return; // one at a time: on a hung drive this would pile up a thread per check
    const QString path = filePath();
    const FileStamp known = m_document.stamp();
    m_checkedPath = path;
    m_checkedHash = known.hash;
    m_diskCheck.setFuture(runInBackground([path, known] { return Document::check(path, known); }));
}

void EditorTab::handleDiskCheck()
{
    const DiskCheck check = m_diskCheck.result();
    // Saved or reloaded meanwhile: the result is about an older version.
    if (m_checkedPath != filePath() || m_checkedHash != m_document.stamp().hash) {
        emit diskCheckFinished();
        return;
    }

    switch (check.state) {
    case DiskState::Unchanged:
        m_document.setStamp(check.stamp); // same content with a new date: don't read it again next time
        m_changeBar->hide();
        break;
    case DiskState::Changed:
        if (isModified()) {
            m_pendingStamp = check.stamp;
            m_changeBar->showChanged(displayName());
        } else if (QString error; reload(error)) {
            emit statusMessage(tr("Reloaded %1: it changed on disk").arg(displayName()));
        } else {
            emit statusMessage(tr("Cannot reload %1: %2").arg(displayName(), error));
        }
        break;
    case DiskState::Missing:
        m_pendingStamp = FileStamp();
        m_changeBar->showMissing(displayName());
        break;
    case DiskState::Unknown:
        break; // a temporary problem: the next check or a save will tell
    }
    emit diskCheckFinished();
}

void EditorTab::keepMyVersion()
{
    m_document.setStamp(m_pendingStamp); // the user has seen this version, so saving replaces it unasked
    m_changeBar->hide();
}
