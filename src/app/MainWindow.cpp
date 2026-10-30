#include "app/MainWindow.h"

#include "editor/MarkdownEditor.h"
#include "panels/FolderPanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>

namespace {

const QString fileFilter = QStringLiteral("Markdown (*.md *.markdown);;Text (*.txt);;All files (*)");

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_editor(new MarkdownEditor(this))
    , m_folderPanel(new FolderPanel(this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
{
    m_splitter->addWidget(m_folderPanel);
    m_splitter->addWidget(m_editor);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 800});
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);
    setAcceptDrops(true);

    createMenus();

    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &QWidget::setWindowModified);
    connect(m_editor, &MarkdownEditor::openFileRequested, this, &MainWindow::openFile);
    connect(m_editor, &MarkdownEditor::filesDropped, this, &MainWindow::openFiles);
    connect(m_folderPanel, &FolderPanel::fileActivated, this, &MainWindow::openFile);
    connect(m_folderPanel, &FolderPanel::openFolderRequested, this, &MainWindow::openFolderDialog);

    QSettings settings;
    if (!restoreGeometry(settings.value("window/geometry").toByteArray()))
        resize(1100, 750);
    m_splitter->restoreState(settings.value("window/splitter").toByteArray());
    m_folderPanel->setVisible(settings.value("window/folderPanel", true).toBool());

    updateTitle();
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newFile);
    file->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::openFileDialog);
    file->addAction(tr("Open &Folder…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this,
                    &MainWindow::openFolderDialog);
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Save &As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Undo"), QKeySequence::Undo, m_editor, &QPlainTextEdit::undo);
    edit->addAction(tr("&Redo"), QKeySequence::Redo, m_editor, &QPlainTextEdit::redo);
    edit->addSeparator();
    edit->addAction(tr("Cu&t"), QKeySequence::Cut, m_editor, &QPlainTextEdit::cut);
    edit->addAction(tr("&Copy"), QKeySequence::Copy, m_editor, &QPlainTextEdit::copy);
    edit->addAction(tr("&Paste"), QKeySequence::Paste, m_editor, &QPlainTextEdit::paste);
    edit->addAction(tr("Select &All"), QKeySequence::SelectAll, m_editor, &QPlainTextEdit::selectAll);

    QMenu *format = menuBar()->addMenu(tr("F&ormat"));
    const struct {
        QString name;
        QKeySequence shortcut;
        QString marker;
    } wraps[] = {
        {tr("&Bold"), QKeySequence(Qt::CTRL | Qt::Key_B), QStringLiteral("**")},
        {tr("&Italic"), QKeySequence(Qt::CTRL | Qt::Key_I), QStringLiteral("*")},
        {tr("&Strikethrough"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), QStringLiteral("~~")},
        {tr("&Highlight"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), QStringLiteral("==")},
        {tr("Inline &Code"), QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), QStringLiteral("`")},
    };
    for (const auto &wrap : wraps) {
        const QString marker = wrap.marker;
        format->addAction(wrap.name, wrap.shortcut, this, [this, marker] { m_editor->toggleWrap(marker); });
    }

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *togglePanel = view->addAction(tr("&Folder Panel"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                                           [this] { m_folderPanel->setVisible(!m_folderPanel->isVisible()); });
    togglePanel->setCheckable(true);
    connect(view, &QMenu::aboutToShow, this, [this, togglePanel] {
        togglePanel->setChecked(m_folderPanel->isVisible());
    });
    view->addSeparator();
    view->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, m_editor, [this] { m_editor->zoomIn(1); });
    view->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, m_editor, [this] { m_editor->zoomOut(1); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About MDEditor"), this, &MainWindow::about);
}

void MainWindow::restoreSession(const QString &file)
{
    QSettings settings;
    const QString folder = settings.value("session/folder").toString();
    if (!folder.isEmpty() && QFileInfo(folder).isDir())
        openFolder(folder);

    const QString toOpen = file.isEmpty() ? settings.value("session/file").toString() : file;
    if (!toOpen.isEmpty() && (QFileInfo::exists(toOpen) || !file.isEmpty()))
        openFile(toOpen);
}

bool MainWindow::openFile(const QString &path)
{
    if (QFileInfo(path).absoluteFilePath() == m_document.filePath())
        return true;
    if (!maybeSave())
        return false;

    QString text;
    QString error;
    Document document;
    if (!document.load(path, text, error)) {
        QMessageBox::warning(this, tr("Open File"),
                             tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }

    m_document = document;
    m_editor->setMarkdown(text);
    m_editor->document()->setModified(false);
    m_editor->setBaseDirectory(QFileInfo(m_document.filePath()).absolutePath());
    if (m_folderPanel->rootPath().isEmpty())
        openFolder(QFileInfo(m_document.filePath()).absolutePath());
    m_folderPanel->selectFile(m_document.filePath());
    updateTitle();
    return true;
}

void MainWindow::openFolder(const QString &path)
{
    m_folderPanel->setRootPath(path);
    m_folderPanel->setVisible(true);
    if (!m_document.isUntitled())
        m_folderPanel->selectFile(m_document.filePath());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/splitter", m_splitter->saveState());
    settings.setValue("window/folderPanel", m_folderPanel->isVisible());
    settings.setValue("session/file", m_document.filePath());
    settings.setValue("session/folder", m_folderPanel->rootPath());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    openFiles(paths);
}

void MainWindow::openFiles(const QStringList &paths)
{
    // Single-document window: a dropped folder opens in the panel, the first file in the editor.
    for (const QString &path : paths) {
        if (QFileInfo(path).isDir()) {
            openFolder(path);
            return;
        }
    }
    if (!paths.isEmpty())
        openFile(paths.first());
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    m_document.reset();
    m_editor->setMarkdown(QString());
    m_editor->document()->setModified(false);
    m_editor->setBaseDirectory(m_folderPanel->rootPath());
    updateTitle();
}

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open File"), dialogDirectory(), fileFilter);
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::openFolderDialog()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder"), dialogDirectory());
    if (!path.isEmpty())
        openFolder(path);
}

bool MainWindow::save()
{
    return m_document.isUntitled() ? saveAs() : saveTo(m_document.filePath());
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), dialogDirectory(), fileFilter);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".md");
    return saveTo(path);
}

bool MainWindow::saveTo(const QString &path)
{
    QString error;
    if (!m_document.save(path, m_editor->markdown(), error)) {
        QMessageBox::warning(this, tr("Save File"),
                             tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    m_editor->document()->setModified(false);
    m_editor->setBaseDirectory(QFileInfo(m_document.filePath()).absolutePath());
    m_folderPanel->selectFile(m_document.filePath());
    updateTitle();
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Changes"), tr("%1 has unsaved changes. Save them?").arg(m_document.displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1[*] - MDEditor").arg(m_document.displayName()));
    setWindowFilePath(m_document.filePath());
    setWindowModified(m_editor->document()->isModified());
}

QString MainWindow::dialogDirectory() const
{
    if (!m_document.isUntitled())
        return QFileInfo(m_document.filePath()).absolutePath();
    if (!m_folderPanel->rootPath().isEmpty())
        return m_folderPanel->rootPath();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About MDEditor"),
                       tr("<h3>MDEditor %1</h3>"
                          "<p>A lightweight Markdown editor with live preview.</p>"
                          "<p>Released under the MIT License.</p>")
                           .arg(QApplication::applicationVersion()));
}
