#include "app/MainWindow.h"

#include "app/EditorTab.h"
#include "app/FindBar.h"
#include "app/RecentFiles.h"
#include "app/AppSettings.h"
#include "app/CloudDrive.h"
#include "app/SettingsDialog.h"
#include "app/SymbolPicker.h"
#include "app/TabCloseButton.h"
#include "app/TablePicker.h"
#include "editor/EditorTheme.h"
#include "editor/MarkdownEditor.h"
#include "panels/Sidebar.h"
#include "plugins/PluginManager.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

#include <utility>

namespace {

const QString fileFilter = QStringLiteral("Markdown (*.md *.markdown);;Text (*.txt);;All files (*)");

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_sidebar(new Sidebar(this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
{
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->installEventFilter(this);

    m_tableButton = new QToolButton(m_tabs);
    m_tableButton->setAutoRaise(true);
    m_tableButton->setToolTip(tr("Insert table"));
    m_tableButton->setFocusPolicy(Qt::NoFocus);
    connect(m_tableButton, &QToolButton::clicked, this, [this] {
        showTablePicker(m_tableButton->mapToGlobal(QPoint(m_tableButton->width(), m_tableButton->height())), true);
    });
    m_symbolButton = new QToolButton(m_tabs);
    m_symbolButton->setAutoRaise(true);
    m_symbolButton->setToolTip(tr("Insert symbol or emoji"));
    m_symbolButton->setFocusPolicy(Qt::NoFocus);
    connect(m_symbolButton, &QToolButton::clicked, this, &MainWindow::toggleSymbolPicker);

    auto *corner = new QWidget(m_tabs); // the corner takes one widget
    auto *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(0);
    cornerLayout->addWidget(m_symbolButton);
    cornerLayout->addWidget(m_tableButton);
    m_tabs->setCornerWidget(corner, Qt::TopRightCorner);
    applyStyle();

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_tabs);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 800});
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);
    setAcceptDrops(true);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        updateWindowTitle();
        if (EditorTab *tab = currentTab()) {
            if (!tab->isUntitled())
                m_sidebar->selectFile(tab->filePath());
            tab->setFocus();
        }
    });
    connect(m_sidebar, &Sidebar::fileActivated, this, &MainWindow::openFile);
    connect(m_sidebar, &Sidebar::openFolderRequested, this, &MainWindow::openFolderDialog);
    connect(m_sidebar, &Sidebar::openVaultRequested, this, &MainWindow::openVaultDialog);
    m_sidebar->setVaultRoot(AppSettings::instance().value(AppSettings::vaultPathKey).toString());
    connect(m_sidebar, &Sidebar::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 8000); });

    m_cloudDrive = new CloudDrive(this);
    connect(m_cloudDrive, &CloudDrive::connected, this, [this](const QString &path, bool automatic) {
        statusBar()->showMessage(tr("Google Drive connected"), 8000);
        openPendingDriveItems();
        if (!automatic)
            openFolder(path);
    });
    connect(m_cloudDrive, &CloudDrive::failed, this,
            [this](const QString &message) { statusBar()->showMessage(message, 15000); });

    createMenus();
    statusBar()->setSizeGripEnabled(false);
    // Coming back to the window is when changes made elsewhere matter. No file watcher: they are
    // unreliable on network drives, and a watcher's stat calls could hang on a disconnected one.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            return;
        for (int i = 0; i < m_tabs->count(); ++i)
            tabAt(i)->checkDisk();
    });
    addTab();

    PluginManager &plugins = PluginManager::instance();
    plugins.setEditorProvider([this] { return currentEditor(); });
    plugins.setMessageSink([this](const QString &message) { statusBar()->showMessage(message, 8000); });
    connect(&plugins, &PluginManager::commandsChanged, this, &MainWindow::populatePluginsMenu);
    plugins.reload();

    QSettings settings;
    if (!restoreGeometry(settings.value("window/geometry").toByteArray()))
        resize(1100, 750);
    m_splitter->restoreState(settings.value("window/splitter").toByteArray());
    m_sidebar->setVisible(settings.value("window/folderPanel", true).toBool());
    m_sidebar->restoreLayout();
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New Tab"), QKeySequence::New, this, &MainWindow::newFile);
    file->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::openFileDialog);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::populateRecentMenu);
    file->addAction(tr("Open &Folder…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this,
                    &MainWindow::openFolderDialog);
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] { save(currentTab()); });
    file->addAction(tr("Save &As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this,
                    [this] { saveAs(currentTab()); });
    file->addSeparator();
    file->addAction(tr("Open &Vault…"), this, &MainWindow::openVaultDialog);
    file->addAction(tr("Clos&e Vault"), this, [this] { openVault(QString()); });
    file->addAction(tr("&Google Drive…"), this, [this] { showSettings(QStringLiteral("Google Drive")); });
    file->addAction(tr("Se&ttings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this] { showSettings(); });
    file->addSeparator();
    file->addAction(tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this,
                    [this] { closeTab(m_tabs->currentIndex()); });
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Undo"), QKeySequence::Undo, this, [this] { currentEditor()->undo(); });
    edit->addAction(tr("&Redo"), QKeySequence::Redo, this, [this] { currentEditor()->redo(); });
    edit->addSeparator();
    edit->addAction(tr("Cu&t"), QKeySequence::Cut, this, [this] { currentEditor()->cut(); });
    edit->addAction(tr("&Copy"), QKeySequence::Copy, this, [this] { currentEditor()->copy(); });
    edit->addAction(tr("&Paste"), QKeySequence::Paste, this, [this] { currentEditor()->paste(); });
    edit->addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] { currentEditor()->selectAll(); });
    edit->addSeparator();
    edit->addAction(tr("&Find…"), QKeySequence::Find, this, [this] { currentTab()->findBar()->openFind(); });
    edit->addAction(tr("R&eplace…"), QKeySequence(Qt::CTRL | Qt::Key_H), this,
                    [this] { currentTab()->findBar()->openReplace(); });
    edit->addAction(tr("Find &Next"), QKeySequence::FindNext, this, [this] { currentTab()->findBar()->findNext(); });
    edit->addAction(tr("Find Pre&vious"), QKeySequence::FindPrevious, this,
                    [this] { currentTab()->findBar()->findPrevious(); });

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
        format->addAction(wrap.name, wrap.shortcut, this, [this, marker] { currentEditor()->toggleWrap(marker); });
    }
    format->addSeparator();
    format->addAction(tr("&Prettify"), MarkdownEditor::prettifyShortcut(), this,
                      [this] { currentEditor()->formatAtCursor(); });
    format->addAction(tr("Prettify &Document"), MarkdownEditor::prettifyDocumentShortcut(), this,
                      [this] { currentEditor()->formatDocument(); });
    format->addSeparator();
    format->addAction(tr("Insert &Table…"), this, [this] {
        MarkdownEditor *editor = currentEditor();
        showTablePicker(editor->viewport()->mapToGlobal(editor->cursorRect().bottomLeft()), false);
    });
    format->addAction(tr("Insert S&ymbol…"), this, &MainWindow::toggleSymbolPicker);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *togglePanel = view->addAction(tr("&Sidebar"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                                           [this] { m_sidebar->setVisible(!m_sidebar->isVisible()); });
    togglePanel->setCheckable(true);
    connect(view, &QMenu::aboutToShow, this, [this, togglePanel] {
        togglePanel->setChecked(m_sidebar->isVisible());
    });
    view->addSeparator();
    view->addAction(tr("&Next Tab"), QKeySequence::NextChild, this, [this] {
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    view->addAction(tr("&Previous Tab"), QKeySequence::PreviousChild, this, [this] {
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });
    view->addSeparator();
    view->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, this, [this] { currentEditor()->zoomIn(1); });
    view->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, this, [this] { currentEditor()->zoomOut(1); });

    m_pluginsMenu = menuBar()->addMenu(tr("&Plugins"));
    populatePluginsMenu();

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About MDEditor"), this, &MainWindow::about);
}

void MainWindow::populatePluginsMenu()
{
    m_pluginsMenu->clear();
    const QList<PluginManager::Command> commands = PluginManager::instance().commands();
    for (const PluginManager::Command &command : commands) {
        const QString plugin = command.plugin;
        const QString id = command.id;
        QAction *action = m_pluginsMenu->addAction(command.name, this, [plugin, id] {
            PluginManager::instance().runCommand(plugin, id);
        });
        action->setShortcut(command.shortcut);
    }
    if (commands.isEmpty())
        m_pluginsMenu->addAction(tr("No plugin commands"))->setEnabled(false);
    m_pluginsMenu->addSeparator();
    m_pluginsMenu->addAction(tr("&Manage Plugins…"), this, [this] { showSettings(QStringLiteral("Plugins")); });
    m_pluginsMenu->addAction(tr("&Reload Plugins"), this, [] { PluginManager::instance().reload(); });
}

// Flat, Obsidian-like tabs; colors are derived from the palette so light and dark modes both work.
void MainWindow::applyStyle()
{
    const QPalette p = palette();
    const QColor window = p.color(QPalette::Window);
    const QColor base = p.color(QPalette::Base);
    const QColor text = p.color(QPalette::Text);

    m_tabs->setStyleSheet(QStringLiteral(R"(
        QTabWidget::pane { border: none; }
        QTabBar { background: %1; }
        QTabBar::tab {
            background: transparent;
            color: %4;
            border: none;
            border-top-left-radius: 7px;
            border-top-right-radius: 7px;
            margin: 5px 1px 0 1px;
            padding: 6px 4px 6px 12px;
            min-width: 60px;
            max-width: 220px;
        }
        QTabBar::tab:selected { background: %2; color: %3; }
        QTabBar::tab:hover:!selected { background: %5; color: %3; }
    )")
                              .arg(window.name(), base.name(), text.name(),
                                   EditorTheme::mix(window, text, 0.55).name(),
                                   EditorTheme::mix(window, text, 0.08).name()));
    m_tableButton->setIcon(tableIcon());
    m_symbolButton->setIcon(symbolIcon());
}

void MainWindow::showTablePicker(const QPoint &globalPos, bool alignRight)
{
    auto *picker = new TablePicker(this);
    connect(picker, &TablePicker::picked, this, [this](int rows, int columns) {
        currentEditor()->insertTable(rows, columns);
    }, Qt::QueuedConnection); // back in the event loop, so the popup's X11 grab has been released
    picker->popup(alignRight ? globalPos - QPoint(picker->width(), 0) : globalPos);
}

void MainWindow::toggleSymbolPicker()
{
    if (!m_symbolPicker) {
        m_symbolPicker = new SymbolPicker(m_symbolButton, this);
        connect(m_symbolPicker, &SymbolPicker::symbolPicked, this, [this](const QString &symbol) {
            currentEditor()->insertPlainText(symbol); // the picker takes no focus, so typing just goes on
        });
    }
    if (m_symbolPicker->isVisible())
        m_symbolPicker->hide();
    else
        m_symbolPicker->showAt(m_symbolButton->mapToGlobal(QPoint(m_symbolButton->width(), m_symbolButton->height())));
}

// A small grid icon drawn in the current text color, so it fits light and dark themes.
QIcon MainWindow::tableIcon() const
{
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(QSize(16, 16) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor color = palette().color(QPalette::WindowText);
    color.setAlphaF(0.75f);
    painter.setPen(QPen(color, 1.2));
    const QRectF frame(1.5, 2.5, 13, 11);
    painter.drawRoundedRect(frame, 2, 2);
    painter.drawLine(QPointF(frame.left(), 6.2), QPointF(frame.right(), 6.2));
    painter.drawLine(QPointF(frame.left(), 9.9), QPointF(frame.right(), 9.9));
    painter.drawLine(QPointF(6.0, frame.top()), QPointF(6.0, frame.bottom()));
    painter.drawLine(QPointF(10.5, frame.top()), QPointF(10.5, frame.bottom()));
    return QIcon(pixmap);
}

// An omega in the same muted text color as the table icon.
QIcon MainWindow::symbolIcon() const
{
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(QSize(16, 16) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor color = palette().color(QPalette::WindowText);
    color.setAlphaF(0.75f);
    painter.setPen(color);
    QFont font = painter.font();
    font.setPixelSize(14);
    painter.setFont(font);
    painter.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, QStringLiteral("Ω"));
    return QIcon(pixmap);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyStyle();
}

void MainWindow::restoreSession(const QStringList &files)
{
    QSettings settings;
    const QString folder = settings.value("session/folder").toString();
    // The mount may not be up yet: such items wait for it instead of looking like deleted files. Even
    // checking them here could hang on a mount left behind by a lost connection.
    if (!folder.isEmpty() && m_cloudDrive->contains(folder))
        m_pendingFolder = folder;
    else if (!folder.isEmpty() && QFileInfo(folder).isDir())
        openFolder(folder);

    if (!files.isEmpty()) {
        for (const QString &path : files)
            openFile(path);
        m_cloudDrive->start();
        return;
    }

    const QStringList previous = settings.value("session/files").toStringList();
    const QString current = settings.value("session/current").toString();
    for (const QString &path : previous) {
        if (m_cloudDrive->contains(path))
            m_pendingFiles << path;
        else if (QFileInfo::exists(path))
            openFile(path);
    }
    m_pendingCurrent = current;
    const int index = findTab(current);
    if (index >= 0)
        m_tabs->setCurrentIndex(index);
    m_cloudDrive->start();
}

void MainWindow::openPendingDriveItems()
{
    if (!m_pendingFolder.isEmpty())
        openFolder(std::exchange(m_pendingFolder, QString()));
    const QStringList files = std::exchange(m_pendingFiles, QStringList());
    for (const QString &path : files) {
        if (QFileInfo::exists(path))
            openFile(path);
    }
    const int index = findTab(m_pendingCurrent);
    if (index >= 0)
        m_tabs->setCurrentIndex(index);
}

bool MainWindow::openFile(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const int existing = findTab(absolute);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return true;
    }

    EditorTab *current = currentTab();
    EditorTab *tab = current && current->isPristine() ? current : addTab();
    QString error;
    if (!tab->load(absolute, error)) {
        if (tab != current) {
            m_tabs->removeTab(m_tabs->indexOf(tab));
            tab->deleteLater();
        }
        QMessageBox::warning(this, tr("Open File"),
                             tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(absolute), error));
        return false;
    }

    m_tabs->setCurrentWidget(tab);
    RecentFiles::add(absolute);
    if (m_sidebar->rootPath().isEmpty())
        openFolder(QFileInfo(absolute).absolutePath());
    m_sidebar->selectFile(absolute);
    return true;
}

void MainWindow::openFolder(const QString &path)
{
    m_sidebar->setRootPath(path);
    m_sidebar->setVisible(true);
    if (EditorTab *tab = currentTab(); tab && !tab->isUntitled())
        m_sidebar->selectFile(tab->filePath());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!maybeSave(tabAt(i))) {
            event->ignore();
            return;
        }
    }

    QStringList files;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!tabAt(i)->isUntitled())
            files << tabAt(i)->filePath();
    }
    files << m_pendingFiles; // the drive didn't connect this time: keep them for the next one

    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/splitter", m_splitter->saveState());
    settings.setValue("window/folderPanel", m_sidebar->isVisible());
    m_sidebar->saveLayout();
    settings.setValue("session/files", files);
    settings.setValue("session/current", currentTab()->filePath());
    settings.setValue("session/folder", m_pendingFolder.isEmpty() ? m_sidebar->rootPath() : m_pendingFolder);
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

// Middle click closes a tab.
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tabs->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::MiddleButton) {
            const int index = m_tabs->tabBar()->tabAt(mouse->position().toPoint());
            if (index >= 0) {
                closeTab(index);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

EditorTab *MainWindow::currentTab() const
{
    return qobject_cast<EditorTab *>(m_tabs->currentWidget());
}

EditorTab *MainWindow::tabAt(int index) const
{
    return qobject_cast<EditorTab *>(m_tabs->widget(index));
}

MarkdownEditor *MainWindow::currentEditor() const
{
    return currentTab()->editor();
}

int MainWindow::findTab(const QString &path) const
{
    if (path.isEmpty())
        return -1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (RecentFiles::samePath(tabAt(i)->filePath(), path))
            return i;
    }
    return -1;
}

EditorTab *MainWindow::addTab()
{
    auto *tab = new EditorTab(m_tabs);
    tab->editor()->setBaseDirectory(m_sidebar->rootPath());
    connect(tab, &EditorTab::stateChanged, this, [this, tab] {
        updateTabTitle(tab);
        if (tab == currentTab())
            updateWindowTitle();
    });
    connect(tab->editor(), &MarkdownEditor::openFileRequested, this, &MainWindow::openFile);
    connect(tab->editor(), &MarkdownEditor::filesDropped, this, &MainWindow::openFiles);
    connect(tab->editor(), &MarkdownEditor::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 8000); });
    connect(tab, &EditorTab::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 8000); });

    const int index = m_tabs->addTab(tab, QString());
    auto *closeButton = new TabCloseButton(m_tabs->tabBar());
    connect(closeButton, &TabCloseButton::clicked, this, [this, tab] { closeTab(m_tabs->indexOf(tab)); });
    m_tabs->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);

    m_tabs->setCurrentIndex(index);
    updateTabTitle(tab);
    updateWindowTitle();
    tab->setFocus();
    return tab;
}

// The window always keeps at least one tab.
bool MainWindow::closeTab(int index)
{
    EditorTab *tab = tabAt(index);
    if (!tab || !maybeSave(tab))
        return false;
    m_tabs->removeTab(index);
    tab->deleteLater();
    if (m_tabs->count() == 0)
        addTab();
    return true;
}

void MainWindow::newFile()
{
    addTab();
}

void MainWindow::openFileDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open File"), dialogDirectory(), fileFilter);
    for (const QString &path : paths)
        openFile(path);
}

void MainWindow::openVaultDialog()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Open Vault"), dialogDirectory());
    if (!path.isEmpty())
        openVault(path);
}

void MainWindow::openVault(const QString &path)
{
    AppSettings::instance().setValue(AppSettings::vaultPathKey, path);
    m_sidebar->setVaultRoot(path);
}

void MainWindow::openFolderDialog()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder"), dialogDirectory());
    if (!path.isEmpty())
        openFolder(path);
}

void MainWindow::openFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (QFileInfo(path).isDir())
            openFolder(path);
        else
            openFile(path);
    }
}

bool MainWindow::save(EditorTab *tab)
{
    return tab->isUntitled() ? saveAs(tab) : saveTo(tab, tab->filePath());
}

bool MainWindow::saveAs(EditorTab *tab)
{
    m_tabs->setCurrentWidget(tab);
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), dialogDirectory(), fileFilter);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".md");
    return saveTo(tab, path);
}

bool MainWindow::saveTo(EditorTab *tab, const QString &path, bool overwrite)
{
    QString error;
    const EditorTab::SaveResult result = tab->save(path, error, overwrite);
    if (result == EditorTab::SaveResult::Conflict)
        return resolveConflict(tab, path);
    if (result == EditorTab::SaveResult::Failed) {
        QMessageBox::warning(this, tr("Save File"),
                             tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    RecentFiles::add(tab->filePath());
    m_sidebar->selectFile(tab->filePath());
    return true;
}

bool MainWindow::resolveConflict(EditorTab *tab, const QString &path)
{
    m_tabs->setCurrentWidget(tab);
    const bool missing = tab->conflictState() == DiskState::Missing;

    QMessageBox box(QMessageBox::Warning, tr("File Changed on Disk"),
                    missing ? tr("%1 is no longer available. It may have been deleted or moved, or its drive may be "
                                 "disconnected.").arg(tab->displayName())
                            : tr("%1 was changed outside MDEditor since you opened it. Saving now replaces those "
                                 "changes.").arg(tab->displayName()),
                    QMessageBox::Cancel, this);
    QPushButton *overwrite = box.addButton(missing ? tr("Save Anyway") : tr("Overwrite"), QMessageBox::DestructiveRole);
    QPushButton *saveCopy = box.addButton(tr("Save As…"), QMessageBox::ActionRole);
    QPushButton *reload = missing ? nullptr : box.addButton(tr("Discard Mine and Reload"), QMessageBox::ActionRole);
    box.setDefaultButton(saveCopy);
    box.exec();

    if (box.clickedButton() == overwrite)
        return saveTo(tab, path, true);
    if (box.clickedButton() == saveCopy)
        return saveAs(tab);
    if (reload && box.clickedButton() == reload) {
        QString error;
        if (tab->reload(error))
            return true;
        QMessageBox::warning(this, tr("Reload File"),
                             tr("Cannot reload %1:\n%2").arg(QDir::toNativeSeparators(path), error));
    }
    return false;
}

bool MainWindow::maybeSave(EditorTab *tab)
{
    if (!tab->isModified())
        return true;
    m_tabs->setCurrentWidget(tab);
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Changes"), tr("%1 has unsaved changes. Save them?").arg(tab->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save(tab);
    return answer == QMessageBox::Discard;
}

void MainWindow::updateTabTitle(EditorTab *tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0)
        return;
    m_tabs->setTabText(index, tab->displayName());
    if (auto *button = qobject_cast<TabCloseButton *>(m_tabs->tabBar()->tabButton(index, QTabBar::RightSide)))
        button->setModified(tab->isModified());
    m_tabs->setTabToolTip(index, tab->isUntitled() ? QString() : QDir::toNativeSeparators(tab->filePath()));
}

void MainWindow::updateWindowTitle()
{
    EditorTab *tab = currentTab();
    if (!tab)
        return;
    setWindowTitle(QStringLiteral("%1[*] - MDEditor").arg(tab->displayName()));
    setWindowFilePath(tab->filePath());
    setWindowModified(tab->isModified());
}

void MainWindow::populateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = RecentFiles::list();
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo info(files[i]);
        QString name = info.fileName();
        name.replace(u'&', QStringLiteral("&&"));
        const QString directory = m_recentMenu->fontMetrics().elidedText(
            QDir::toNativeSeparators(info.absolutePath()), Qt::ElideMiddle, 360);
        const QString label = QStringLiteral("%1%2   %3")
                                  .arg(i < 9 ? QStringLiteral("&%1  ").arg(i + 1) : QString(), name,
                                       QString(directory).replace(u'&', QStringLiteral("&&")));
        QAction *action = m_recentMenu->addAction(label);
        action->setEnabled(info.exists()); // keep files on unplugged drives, just disabled
        const QString path = files[i];
        connect(action, &QAction::triggered, this, [this, path] { openFile(path); });
    }

    if (files.isEmpty())
        m_recentMenu->addAction(tr("No Recent Files"))->setEnabled(false);
    m_recentMenu->addSeparator();
    QAction *clear = m_recentMenu->addAction(tr("&Clear Recent Files"), this, [] { RecentFiles::clear(); });
    clear->setEnabled(!files.isEmpty());
}

QString MainWindow::dialogDirectory() const
{
    if (EditorTab *tab = currentTab(); tab && !tab->isUntitled())
        return QFileInfo(tab->filePath()).absolutePath();
    if (!m_sidebar->rootPath().isEmpty())
        return m_sidebar->rootPath();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void MainWindow::showSettings(const QString &page)
{
    if (!m_settingsDialog)
        m_settingsDialog = new SettingsDialog(m_cloudDrive, this);
    if (!page.isEmpty())
        m_settingsDialog->showPage(page);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About MDEditor"),
                       tr("<h3>MDEditor %1</h3>"
                          "<p>A lightweight Markdown editor with live preview.</p>"
                          "<p>Released under the MIT License.</p>")
                           .arg(QApplication::applicationVersion()));
}
