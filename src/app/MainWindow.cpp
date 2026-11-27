#include "app/MainWindow.h"

#include "app/Bookmarks.h"
#include "app/EditorTab.h"
#include "app/FileActions.h"
#include "app/FindBar.h"
#include "app/RecentFiles.h"
#include "app/CloudDrive.h"
#include "app/SettingsDialog.h"
#include "app/SymbolPicker.h"
#include "app/TabCloseButton.h"
#include "app/TablePicker.h"
#include "document/Encodings.h"
#include "editor/EditorTheme.h"
#include "editor/MarkdownEditor.h"
#include "panels/FolderPanel.h"
#include "plugins/PluginManager.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>

#include <utility>

namespace {

// Any text file opens; saving a new document suggests Markdown.
const QString openFilter = QStringLiteral("All files (*);;Markdown (*.md *.markdown);;Text (*.txt)");
const QString saveFilter = QStringLiteral("Markdown (*.md *.markdown);;Text (*.txt);;All files (*)");

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabs(nullptr)
    , m_paneSplitter(new QSplitter(Qt::Horizontal, this))
    , m_folderPanel(new FolderPanel(this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
{
    m_paneSplitter->setChildrenCollapsible(false);
    m_tabs = createPane();

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

    m_splitter->addWidget(m_folderPanel);
    m_splitter->addWidget(m_paneSplitter);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 800});
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);
    setAcceptDrops(true);

    // The pane with the focus is the current one: menus and shortcuts act on its tab.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        for (QTabWidget *pane : std::as_const(m_panes)) {
            if (now && pane->isAncestorOf(now)) {
                setCurrentPane(pane);
                break;
            }
        }
    });
    connect(m_folderPanel, &FolderPanel::fileActivated, this, &MainWindow::openFile);
    connect(m_folderPanel, &FolderPanel::openFolderRequested, this, &MainWindow::openFolderDialog);
    connect(m_folderPanel, &FolderPanel::renameRequested, this, &MainWindow::renamePath);
    connect(m_folderPanel, &FolderPanel::deleteRequested, this, &MainWindow::deletePath);
    connect(m_folderPanel, &FolderPanel::duplicateRequested, this, &MainWindow::duplicateFile);
    connect(m_folderPanel, &FolderPanel::newNoteRequested, this, &MainWindow::newNote);
    connect(m_folderPanel, &FolderPanel::newFolderRequested, this, &MainWindow::newFolder);
    connect(m_folderPanel, &FolderPanel::rootRequested, this, &MainWindow::openFolder);
    connect(m_folderPanel, &FolderPanel::moveRequested, this, &MainWindow::movePath);

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
    m_encodingButton = new QToolButton(this);
    m_encodingButton->setAutoRaise(true);
    m_encodingButton->setFocusPolicy(Qt::NoFocus);
    m_encodingButton->setToolTip(tr("Encoding and line endings"));
    m_encodingButton->setPopupMode(QToolButton::InstantPopup);
    m_encodingButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
    auto *encodingMenu = new QMenu(m_encodingButton);
    connect(encodingMenu, &QMenu::aboutToShow, this, [this, encodingMenu] { populateEncodingMenu(encodingMenu); });
    m_encodingButton->setMenu(encodingMenu);
    statusBar()->addPermanentWidget(m_encodingButton);
    // Coming back to the window is when changes made elsewhere matter. No file watcher: they are
    // unreliable on network drives, and a watcher's stat calls could hang on a disconnected one.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            return;
        for (EditorTab *tab : allTabs())
            tab->checkDisk();
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
    m_folderPanel->setVisible(settings.value("window/folderPanel", true).toBool());
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
    // F2 renames like in Obsidian: the current file, or what's selected in the folder panel if it has the focus.
    file->addAction(tr("Rena&me…"), QKeySequence(Qt::Key_F2), this, [this] {
        if (!m_folderPanel->renameCurrent() && !currentTab()->isUntitled())
            renameTab(currentTab());
    });
    file->addAction(tr("Mo&ve File To…"), this, [this] {
        if (!currentTab()->isUntitled())
            movePath(currentTab()->filePath(), QString());
    });
    file->addSeparator();
    file->addAction(tr("&Google Drive…"), this, [this] { showSettings(QStringLiteral("Google Drive")); });
    file->addAction(tr("Se&ttings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this] { showSettings(); });
    file->addSeparator();
    file->addAction(tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this,
                    [this] { closeTab(currentTab()); });
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Undo"), QKeySequence::Undo, this, [this] { currentEditor()->undo(); });
    edit->addAction(tr("&Redo"), QKeySequence::Redo, this, [this] { currentEditor()->redo(); });
    edit->addSeparator();
    edit->addAction(tr("Cu&t"), QKeySequence::Cut, this, [this] { currentEditor()->cut(); });
    edit->addAction(tr("&Copy"), QKeySequence::Copy, this, [this] { currentEditor()->copy(); });
    edit->addAction(tr("&Paste"), QKeySequence::Paste, this, [this] { currentEditor()->paste(); });
    edit->addAction(tr("Paste as Plain Te&xt"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), this,
                    [this] { currentEditor()->insertPlainText(QGuiApplication::clipboard()->text()); });
    edit->addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] { currentEditor()->selectAll(); });
    edit->addSeparator();
    edit->addAction(tr("&Find…"), QKeySequence::Find, this, [this] { currentTab()->findBar()->openFind(); });
    edit->addAction(tr("R&eplace…"), QKeySequence(Qt::CTRL | Qt::Key_H), this,
                    [this] { currentTab()->findBar()->openReplace(); });
    edit->addAction(tr("Find &Next"), QKeySequence::FindNext, this, [this] { currentTab()->findBar()->findNext(); });
    edit->addAction(tr("Find Pre&vious"), QKeySequence::FindPrevious, this,
                    [this] { currentTab()->findBar()->findPrevious(); });

    // Markdown commands act on Markdown only: in a plain text file their shortcuts do nothing.
    QMenu *format = menuBar()->addMenu(tr("F&ormat"));
    const MarkdownEditor::EditorGetter markdown = [this] { return markdownEditor(); };
    MarkdownEditor::addFormatActions(format, markdown, true);
    format->addSeparator();
    MarkdownEditor::addLinkActions(format, markdown, true);
    format->addSeparator();
    QMenu *paragraph = format->addMenu(tr("Pa&ragraph"));
    MarkdownEditor::addParagraphActions(paragraph, markdown);
    QMenu *insert = format->addMenu(tr("I&nsert"));
    MarkdownEditor::addInsertActions(insert, markdown);
    m_markdownActions = format->actions();
    format->addSeparator();
    format->addAction(tr("&Prettify"), MarkdownEditor::prettifyShortcut(), this,
                      [this] { currentEditor()->formatAtCursor(); });
    format->addAction(tr("Prettify &Document"), MarkdownEditor::prettifyDocumentShortcut(), this,
                      [this] { currentEditor()->formatDocument(); });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *togglePanel = view->addAction(tr("&Folder Panel"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                                           [this] { m_folderPanel->setVisible(!m_folderPanel->isVisible()); });
    togglePanel->setCheckable(true);
    // Per tab: Markdown files start with the live preview, other text files without.
    QAction *livePreview = view->addAction(tr("&Live Preview"), QKeySequence(Qt::CTRL | Qt::Key_E), this, [this] {
        currentEditor()->setMarkdownMode(!currentEditor()->markdownMode());
    });
    livePreview->setCheckable(true);
    QAction *wordWrap = view->addAction(tr("&Word Wrap"), QKeySequence(Qt::ALT | Qt::Key_Z), this, [this] {
        MarkdownEditor *editor = currentEditor();
        editor->setLineWrapMode(editor->lineWrapMode() == QPlainTextEdit::NoWrap ? QPlainTextEdit::WidgetWidth
                                                                                : QPlainTextEdit::NoWrap);
    });
    wordWrap->setCheckable(true);
    connect(view, &QMenu::aboutToShow, this, [this, togglePanel, livePreview, wordWrap] {
        togglePanel->setChecked(m_folderPanel->isVisible());
        livePreview->setChecked(currentEditor()->markdownMode());
        wordWrap->setChecked(currentEditor()->lineWrapMode() != QPlainTextEdit::NoWrap);
    });
    view->addSeparator();
    view->addAction(tr("&Next Tab"), QKeySequence::NextChild, this, [this] {
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    view->addAction(tr("&Previous Tab"), QKeySequence::PreviousChild, this, [this] {
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });
    view->addSeparator();
    view->addAction(tr("Split &Right"), this, [this] { splitTab(currentTab(), Qt::Horizontal); });
    view->addAction(tr("Split &Down"), this, [this] { splitTab(currentTab(), Qt::Vertical); });
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

void MainWindow::updateEncodingButton()
{
    EditorTab *tab = currentTab();
    if (!m_encodingButton || !tab)
        return;
    m_encodingButton->setText(QStringLiteral("%1  ·  %2").arg(Encodings::label(tab->encoding(), tab->hasBom()),
                                                              tab->usesCrlf() ? QStringLiteral("CRLF") : QStringLiteral("LF")));
}

void MainWindow::populateEncodingMenu(QMenu *menu)
{
    menu->clear();
    EditorTab *tab = currentTab();
    if (!tab)
        return;
    const QPointer<EditorTab> guard(tab);
    const QStringList encodings = Encodings::common();

    QMenu *reopen = menu->addMenu(tr("&Reopen with Encoding"));
    reopen->setEnabled(!tab->isUntitled());
    for (const QString &encoding : encodings) {
        QAction *action = reopen->addAction(Encodings::label(encoding, false));
        action->setCheckable(true);
        action->setChecked(encoding == tab->encoding());
        connect(action, &QAction::triggered, this, [this, guard, encoding] {
            if (!guard)
                return;
            if (guard->isModified()
                && QMessageBox::question(this, tr("Reopen with Encoding"),
                                         tr("%1 has unsaved changes. Discard them and read the file again as %2?")
                                             .arg(guard->displayName(), Encodings::label(encoding, false)))
                    != QMessageBox::Yes)
                return;
            QString error;
            if (!guard->reload(error, encoding))
                QMessageBox::warning(this, tr("Reopen with Encoding"), error);
        });
    }

    QMenu *saveMenu = menu->addMenu(tr("&Save with Encoding"));
    const auto addSave = [&](const QString &encoding, bool bom) {
        QAction *action = saveMenu->addAction(Encodings::label(encoding, bom));
        action->setCheckable(true);
        action->setChecked(encoding == tab->encoding() && bom == tab->hasBom());
        connect(action, &QAction::triggered, this, [this, guard, encoding, bom] {
            if (!guard)
                return;
            const QString previous = guard->encoding();
            const bool previousBom = guard->hasBom();
            guard->setEncoding(encoding, bom);
            if (!save(guard) && guard)
                guard->setEncoding(previous, previousBom); // not saved: nothing changed
        });
    };
    for (const QString &encoding : encodings) {
        addSave(encoding, encoding.startsWith(u"UTF-16")); // UTF-16 without a BOM is hard for others to read
        if (encoding == Encodings::utf8)
            addSave(encoding, true);
    }

    menu->addSeparator();
    for (const bool crlf : {false, true}) {
        QAction *action = menu->addAction(crlf ? tr("Windows Line Endings (CRLF)") : tr("Unix Line Endings (LF)"));
        action->setCheckable(true);
        action->setChecked(tab->usesCrlf() == crlf);
        connect(action, &QAction::triggered, this, [guard, crlf] {
            if (guard)
                guard->setCrlf(crlf);
        });
    }
}

void MainWindow::updateMarkdownActions()
{
    const bool enabled = currentTab() && markdownEditor();
    for (QAction *action : std::as_const(m_markdownActions))
        action->setEnabled(enabled);
}

// Flat, Obsidian-like tabs; colors are derived from the palette so light and dark modes both work.
void MainWindow::applyStyle()
{
    const QPalette p = palette();
    const QColor window = p.color(QPalette::Window);
    const QColor base = p.color(QPalette::Base);
    const QColor text = p.color(QPalette::Text);

    const QString style = QStringLiteral(R"(
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
                                   EditorTheme::mix(window, text, 0.08).name());
    for (QTabWidget *pane : std::as_const(m_panes))
        pane->setStyleSheet(style);
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
    // A second pane: its files, opened there (as twins where the first pane has them too).
    const QStringList second = settings.value("session/files2").toStringList();
    if (!second.isEmpty()) {
        const auto orientation = settings.value("session/splitVertical").toBool() ? Qt::Vertical : Qt::Horizontal;
        QTabWidget *first = m_tabs;
        for (const QString &path : second) {
            if (m_cloudDrive->contains(path) || !QFileInfo::exists(path))
                continue;
            setCurrentPane(first);
            if (!openFile(path))
                continue;
            EditorTab *opened = findTab(path);
            if (m_panes.size() == 1)
                splitTab(opened, orientation);
            else if (opened && paneOf(opened) == first)
                splitTab(opened, orientation);
        }
        // Files only meant for the second pane were opened in the first one to load them: move them over.
        if (m_panes.size() == 2) {
            for (EditorTab *tab : allTabs()) {
                if (paneOf(tab) == first && !tab->twin() && !previous.contains(tab->filePath())
                    && second.contains(tab->filePath()))
                    moveToOtherPane(tab);
            }
            m_paneSplitter->restoreState(settings.value("session/paneSplitter").toByteArray());
        }
        setCurrentPane(first);
    }
    m_pendingCurrent = current;
    if (EditorTab *tab = findTab(current))
        activate(tab);
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
    if (EditorTab *tab = findTab(m_pendingCurrent))
        activate(tab);
}
bool MainWindow::openFile(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (EditorTab *existing = findTab(absolute)) {
        activate(existing);
        return true;
    }

    EditorTab *current = currentTab();
    EditorTab *tab = current && current->isPristine() ? current : addTab();
    QString error;
    bool binary = false;
    if (!tab->load(absolute, error, &binary)) {
        if (tab != current) {
            if (QTabWidget *pane = paneOf(tab))
                pane->removeTab(pane->indexOf(tab));
            tab->deleteLater();
        }
        if (binary) { // an image, a PDF...: what the system opens it with is more useful than an error
            FileActions::openInDefaultApp(absolute);
            statusBar()->showMessage(tr("%1 is not a text file: opened it in its default application")
                                         .arg(QFileInfo(absolute).fileName()), 8000);
            return false;
        }
        QMessageBox::warning(this, tr("Open File"),
                             tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(absolute), error));
        return false;
    }

    activate(tab);
    RecentFiles::add(absolute);
    if (m_folderPanel->rootPath().isEmpty())
        openFolder(QFileInfo(absolute).absolutePath());
    m_folderPanel->selectFile(absolute);
    return true;
}

void MainWindow::openFolder(const QString &path)
{
    m_folderPanel->setRootPath(path);
    m_folderPanel->setVisible(true);
    if (EditorTab *tab = currentTab(); tab && !tab->isUntitled())
        m_folderPanel->selectFile(tab->filePath());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QList<EditorTab *> asked;
    for (EditorTab *tab : allTabs()) {
        if (asked.contains(tab->twin()))
            continue; // the same file, already saved or discarded
        asked << tab;
        if (!maybeSave(tab)) {
            event->ignore();
            return;
        }
    }

    const auto filesOf = [this](QTabWidget *pane) {
        QStringList files;
        for (int i = 0; i < pane->count(); ++i) {
            const auto *tab = qobject_cast<EditorTab *>(pane->widget(i));
            if (tab && !tab->isUntitled())
                files << tab->filePath();
        }
        return files;
    };
    QStringList files = filesOf(m_panes.front());
    files << m_pendingFiles; // the drive didn't connect this time: keep them for the next one

    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/splitter", m_splitter->saveState());
    settings.setValue("window/folderPanel", m_folderPanel->isVisible());
    settings.setValue("session/files", files);
    settings.setValue("session/files2", m_panes.size() > 1 ? filesOf(m_panes[1]) : QStringList());
    settings.setValue("session/splitVertical", m_paneSplitter->orientation() == Qt::Vertical);
    settings.setValue("session/paneSplitter", m_paneSplitter->saveState());
    settings.setValue("session/current", currentTab()->filePath());
    settings.setValue("session/folder", m_pendingFolder.isEmpty() ? m_folderPanel->rootPath() : m_pendingFolder);
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
    if (event->type() == QEvent::MouseButtonRelease) {
        for (QTabWidget *pane : std::as_const(m_panes)) {
            if (watched != pane->tabBar())
                continue;
            const auto *mouse = static_cast<QMouseEvent *>(event);
            const int index = pane->tabBar()->tabAt(mouse->position().toPoint());
            if (mouse->button() == Qt::MiddleButton && index >= 0) {
                closeTab(qobject_cast<EditorTab *>(pane->widget(index)));
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

QList<EditorTab *> MainWindow::allTabs() const
{
    QList<EditorTab *> tabs;
    for (QTabWidget *pane : m_panes) {
        for (int i = 0; i < pane->count(); ++i) {
            if (auto *tab = qobject_cast<EditorTab *>(pane->widget(i)))
                tabs << tab;
        }
    }
    return tabs;
}

QTabWidget *MainWindow::paneOf(const EditorTab *tab) const
{
    for (QTabWidget *pane : m_panes) {
        if (pane->indexOf(const_cast<EditorTab *>(tab)) >= 0)
            return pane;
    }
    return nullptr;
}

void MainWindow::setCurrentPane(QTabWidget *pane)
{
    if (!pane || pane == m_tabs)
        return;
    m_tabs = pane;
    updateWindowTitle();
    updateMarkdownActions();
    updateEncodingButton();
}

void MainWindow::activate(EditorTab *tab)
{
    QTabWidget *pane = paneOf(tab);
    if (!pane)
        return;
    setCurrentPane(pane);
    pane->setCurrentWidget(tab);
    tab->setFocus();
}

QTabWidget *MainWindow::createPane()
{
    auto *pane = new QTabWidget(m_paneSplitter);
    pane->setDocumentMode(true);
    pane->setMovable(true);
    pane->tabBar()->setElideMode(Qt::ElideMiddle);
    pane->tabBar()->setDrawBase(false);
    pane->tabBar()->setExpanding(false);
    pane->tabBar()->installEventFilter(this);
    pane->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(pane->tabBar(), &QWidget::customContextMenuRequested, this,
            [this, pane](const QPoint &pos) { showTabMenu(pane, pos); });
    // The tab bar takes no focus: a click on a tab of the other pane makes that pane current.
    connect(pane, &QTabWidget::tabBarClicked, this, [this, pane](int index) {
        setCurrentPane(pane);
        if (QWidget *tab = pane->widget(index))
            tab->setFocus();
    });
    connect(pane, &QTabWidget::currentChanged, this, [this, pane] {
        if (pane != m_tabs)
            return;
        updateWindowTitle();
        updateMarkdownActions();
        updateEncodingButton();
        if (EditorTab *tab = currentTab()) {
            if (!tab->isUntitled())
                m_folderPanel->selectFile(tab->filePath());
            tab->setFocus();
        }
    });
    m_paneSplitter->addWidget(pane);
    m_panes << pane;
    pane->setStyleSheet(m_panes.front()->styleSheet()); // the second pane: styled like the first
    return pane;
}

void MainWindow::splitTab(EditorTab *tab, Qt::Orientation orientation)
{
    if (!tab)
        return;
    QTabWidget *source = paneOf(tab);
    QTabWidget *target = nullptr;
    if (m_panes.size() == 1) {
        m_paneSplitter->setOrientation(orientation);
        target = createPane();
        const int half = (orientation == Qt::Horizontal ? m_paneSplitter->width() : m_paneSplitter->height()) / 2;
        m_paneSplitter->setSizes({half, half});
    } else {
        target = m_panes[0] == source ? m_panes[1] : m_panes[0];
    }
    if (tab->twin() && paneOf(tab->twin()) == target) {
        activate(tab->twin());
        return;
    }
    EditorTab *copy = addTab(target);
    tab->openTwin(copy);
    activate(copy);
}

void MainWindow::moveToOtherPane(EditorTab *tab)
{
    QTabWidget *source = paneOf(tab);
    if (!source || m_panes.size() < 2)
        return;
    QTabWidget *target = m_panes[0] == source ? m_panes[1] : m_panes[0];
    if (tab->twin() && paneOf(tab->twin()) == target) { // already there: this copy goes
        EditorTab *twin = tab->twin();
        tab->unlinkTwin();
        discardTab(tab);
        activate(twin);
        return;
    }
    // Reparenting keeps the editor, its undo history and cursor.
    TabCloseButton *button = qobject_cast<TabCloseButton *>(source->tabBar()->tabButton(source->indexOf(tab), QTabBar::RightSide));
    source->removeTab(source->indexOf(tab));
    const int index = target->addTab(tab, tab->displayName());
    auto *closeButton = new TabCloseButton(target->tabBar());
    connect(closeButton, &TabCloseButton::clicked, this, [this, tab] { closeTab(tab); });
    target->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);
    if (button)
        button->deleteLater();
    updateTabTitle(tab);
    activate(tab);
    removePaneIfEmpty(source);
}

// The window keeps one pane with at least one tab; an empty second pane goes.
void MainWindow::removePaneIfEmpty(QTabWidget *pane)
{
    if (!pane || pane->count() > 0)
        return;
    if (m_panes.size() == 1) {
        addTab(pane);
        return;
    }
    m_panes.removeOne(pane);
    if (m_tabs == pane)
        m_tabs = nullptr;
    setCurrentPane(m_panes.front());
    if (QWidget *corner = pane->cornerWidget(Qt::TopRightCorner)) {
        // The table and symbol buttons live in the first pane; keep them when it closes.
        corner->setParent(m_panes.front());
        m_panes.front()->setCornerWidget(corner, Qt::TopRightCorner);
        corner->show();
    }
    pane->deleteLater();
    if (EditorTab *tab = currentTab())
        tab->setFocus();
}
MarkdownEditor *MainWindow::currentEditor() const
{
    return currentTab()->editor();
}

MarkdownEditor *MainWindow::markdownEditor() const
{
    MarkdownEditor *editor = currentTab() ? currentEditor() : nullptr;
    return editor && editor->markdownMode() ? editor : nullptr;
}

EditorTab *MainWindow::findTab(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;
    EditorTab *found = nullptr;
    for (EditorTab *tab : allTabs()) {
        if (RecentFiles::samePath(tab->filePath(), path)) {
            if (paneOf(tab) == m_tabs)
                return tab;
            found = found ? found : tab;
        }
    }
    return found;
}
EditorTab *MainWindow::addTab(QTabWidget *pane)
{
    if (!pane)
        pane = m_tabs;
    auto *tab = new EditorTab(pane);
    tab->editor()->setBaseDirectory(m_folderPanel->rootPath());
    connect(tab, &EditorTab::stateChanged, this, [this, tab] {
        updateTabTitle(tab);
        if (tab == currentTab()) {
            updateWindowTitle();
            updateEncodingButton();
        }
    });
    connect(tab->editor(), &MarkdownEditor::openFileRequested, this, &MainWindow::openFile);
    connect(tab->editor(), &MarkdownEditor::filesDropped, this, &MainWindow::openFiles);
    connect(tab->editor(), &MarkdownEditor::markdownModeChanged, this, [this, tab] {
        if (tab == currentTab())
            updateMarkdownActions();
    });
    connect(tab->editor(), &MarkdownEditor::insertTableRequested, this,
            [this](const QPoint &globalPos) { showTablePicker(globalPos, false); });
    connect(tab->editor(), &MarkdownEditor::insertSymbolRequested, this, [this] {
        if (!m_symbolPicker || !m_symbolPicker->isVisible())
            toggleSymbolPicker();
    });
    connect(tab->editor(), &MarkdownEditor::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 8000); });
    connect(tab, &EditorTab::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 8000); });

    const int index = pane->addTab(tab, QString());
    auto *closeButton = new TabCloseButton(pane->tabBar());
    connect(closeButton, &TabCloseButton::clicked, this, [this, tab] { closeTab(tab); });
    pane->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);

    setCurrentPane(pane);
    pane->setCurrentIndex(index);
    updateTabTitle(tab);
    updateWindowTitle();
    tab->setFocus();
    return tab;
}

// The window always keeps at least one tab.
bool MainWindow::closeTab(EditorTab *tab)
{
    QTabWidget *pane = paneOf(tab);
    if (!pane)
        return false;
    // A twin keeps the changes: only the last view of a file asks about them.
    if (!tab->twin() && !maybeSave(tab))
        return false;
    tab->unlinkTwin();
    pane->removeTab(pane->indexOf(tab));
    tab->deleteLater();
    removePaneIfEmpty(pane);
    return true;
}
void MainWindow::newFile()
{
    addTab();
}

void MainWindow::openFileDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open File"), dialogDirectory(), openFilter);
    for (const QString &path : paths)
        openFile(path);
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
    activate(tab);
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), dialogDirectory(), saveFilter);
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
    if (result == EditorTab::SaveResult::Unencodable) {
        activate(tab);
        QMessageBox box(QMessageBox::Warning, tr("Save File"),
                        tr("%1 Saving in that encoding would lose them.\n\nSave %2 as UTF-8 instead?")
                            .arg(error, tab->displayName()),
                        QMessageBox::Cancel, this);
        QPushButton *utf8 = box.addButton(tr("Save as UTF-8"), QMessageBox::AcceptRole);
        box.setDefaultButton(utf8);
        box.exec();
        if (box.clickedButton() != utf8)
            return false;
        tab->setEncoding(Encodings::utf8, false);
        return saveTo(tab, path, overwrite);
    }
    if (result == EditorTab::SaveResult::Failed) {
        QMessageBox::warning(this, tr("Save File"),
                             tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    RecentFiles::add(tab->filePath());
    m_folderPanel->selectFile(tab->filePath());
    return true;
}

bool MainWindow::resolveConflict(EditorTab *tab, const QString &path)
{
    activate(tab);
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
    activate(tab);
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Changes"), tr("%1 has unsaved changes. Save them?").arg(tab->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save(tab);
    return answer == QMessageBox::Discard;
}

void MainWindow::updateTabTitle(EditorTab *tab)
{
    QTabWidget *pane = paneOf(tab);
    const int index = pane ? pane->indexOf(tab) : -1;
    if (index < 0)
        return;
    pane->setTabText(index, tab->displayName());
    if (auto *button = qobject_cast<TabCloseButton *>(pane->tabBar()->tabButton(index, QTabBar::RightSide)))
        button->setModified(tab->isModified());
    pane->setTabToolTip(index, tab->isUntitled() ? QString() : QDir::toNativeSeparators(tab->filePath()));
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
    if (!m_folderPanel->rootPath().isEmpty())
        return m_folderPanel->rootPath();
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

// --- Tab menu and file management ---------------------------------------------------------------

void MainWindow::closeTabs(QTabWidget *pane, const std::function<bool(EditorTab *)> &which)
{
    QList<QPointer<EditorTab>> tabs;
    for (int i = pane->count() - 1; i >= 0; --i)
        tabs << qobject_cast<EditorTab *>(pane->widget(i));
    for (const QPointer<EditorTab> &tab : tabs) {
        if (tab && which(tab) && !closeTab(tab))
            return; // Cancel stops the whole operation
    }
}
void MainWindow::discardTab(EditorTab *tab)
{
    QTabWidget *pane = paneOf(tab);
    if (!pane)
        return;
    tab->unlinkTwin();
    pane->removeTab(pane->indexOf(tab));
    tab->deleteLater();
    removePaneIfEmpty(pane);
}
void MainWindow::showTabMenu(QTabWidget *pane, const QPoint &pos)
{
    const int index = pane->tabBar()->tabAt(pos);
    auto *tab = qobject_cast<EditorTab *>(pane->widget(index));
    if (!tab)
        return;
    setCurrentPane(pane);
    const QString path = tab->filePath();
    const auto label = [](const QString &text, const QKeySequence &shortcut) {
        return text + u'\t' + shortcut.toString(QKeySequence::NativeText);
    };
    // Queued, like every menu here that may open a dialog: the menu is gone and its event loop is done.
    const auto later = [this](std::function<void()> run) {
        return [this, run] { QMetaObject::invokeMethod(this, run, Qt::QueuedConnection); };
    };
    const QPointer<EditorTab> guard(tab);

    QMenu menu(this);
    menu.addAction(label(tr("&Close"), QKeySequence(Qt::CTRL | Qt::Key_W)), this, later([this, guard] {
        if (guard)
            closeTab(guard);
    }));
    const QPointer<QTabWidget> where(pane);
    menu.addAction(tr("Close &Others"), this, later([this, guard, where] {
        if (where)
            closeTabs(where, [guard](EditorTab *t) { return t != guard; });
    }))->setEnabled(pane->count() > 1);
    menu.addAction(tr("Close Tabs to the &Right"), this, later([this, guard, where] {
        if (!where || !guard)
            return;
        const int from = where->indexOf(guard);
        closeTabs(where, [where, from](EditorTab *t) { return where->indexOf(t) > from; });
    }))->setEnabled(index + 1 < pane->count());
    menu.addAction(tr("Close &All"), this, later([this, where] {
        if (where)
            closeTabs(where, [](EditorTab *) { return true; });
    }));
    menu.addSeparator();

    menu.addAction(tr("Split Ri&ght"), this, [this, guard] {
        if (guard)
            splitTab(guard, Qt::Horizontal);
    });
    menu.addAction(tr("Split &Down"), this, [this, guard] {
        if (guard)
            splitTab(guard, Qt::Vertical);
    });
    if (m_panes.size() > 1) {
        menu.addAction(tr("Move to Other Pan&e"), this, later([this, guard] {
            if (guard)
                moveToOtherPane(guard);
        }));
    }
    menu.addSeparator();

    QAction *preview = menu.addAction(label(tr("&Live Preview"), QKeySequence(Qt::CTRL | Qt::Key_E)), this, [guard] {
        if (guard)
            guard->editor()->setMarkdownMode(!guard->editor()->markdownMode());
    });
    preview->setCheckable(true);
    preview->setChecked(tab->editor()->markdownMode());

    if (!tab->isUntitled()) {
        menu.addSeparator();
        FileActions::addCopyPathMenu(menu.addMenu(tr("Copy &Path")), path, m_folderPanel->rootPath());
        menu.addAction(tr("Reveal in &Folder Panel"), this, [this, path] { revealInFolderPanel(path); });
        menu.addAction(FileActions::showInFileManagerLabel(), this, [path] { FileActions::showInFileManager(path); });
        menu.addAction(tr("Open &in Default App"), this, [path] { FileActions::openInDefaultApp(path); });
        menu.addAction(Bookmarks::contains(path) ? tr("Remove &Bookmark") : tr("&Bookmark"), this,
                       [path] { Bookmarks::toggle(path); });
        menu.addSeparator();
        menu.addAction(tr("Re&name…"), this, later([this, guard] {
            if (guard)
                renameTab(guard);
        }));
        menu.addAction(tr("&Move File To…"), this, later([this, path] { movePath(path, QString()); }));
        menu.addAction(tr("Dele&te File"), this, later([this, path] { deletePath(path); }));
    }
    menu.exec(pane->tabBar()->mapToGlobal(pos));
}

void MainWindow::revealInFolderPanel(const QString &path)
{
    if (!FileActions::isSameOrInside(path, m_folderPanel->rootPath()))
        m_folderPanel->setRootPath(QFileInfo(path).absolutePath());
    m_folderPanel->setVisible(true);
    m_folderPanel->selectFile(path);
}

void MainWindow::renameTab(EditorTab *tab)
{
    const QString path = tab->filePath();
    // In place in the folder panel, as Obsidian does, when the file is listed there.
    if (m_folderPanel->isVisible() && m_folderPanel->startRename(path))
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                                               QFileInfo(path).fileName(), &ok);
    if (ok)
        renamePath(path, name.trimmed());
}

void MainWindow::renamePath(const QString &path, const QString &newName)
{
    const QFileInfo info(path);
    if (newName == info.fileName())
        return;
    if (const QString problem = FileActions::checkName(newName); !problem.isEmpty()) {
        QMessageBox::warning(this, tr("Rename"), problem);
        return;
    }
    if (relocate(path, info.dir().filePath(newName), tr("Rename")))
        statusBar()->showMessage(tr("Renamed to %1").arg(newName), 5000);
}

void MainWindow::movePath(const QString &path, const QString &folder)
{
    QString destination = folder;
    if (destination.isEmpty()) {
        const QString start = m_folderPanel->rootPath().isEmpty() ? QFileInfo(path).absolutePath() : m_folderPanel->rootPath();
        destination = QFileDialog::getExistingDirectory(this, tr("Move %1 To").arg(QFileInfo(path).fileName()), start);
        if (destination.isEmpty())
            return;
    }
    if (FileActions::isSameOrInside(destination, path)) {
        QMessageBox::warning(this, tr("Move"), tr("A folder can't be moved into itself."));
        return;
    }
    const QString target = QDir(destination).filePath(QFileInfo(path).fileName());
    if (RecentFiles::samePath(QDir::cleanPath(target), QDir::cleanPath(path)))
        return;
    if (relocate(path, target, tr("Move")))
        statusBar()->showMessage(tr("Moved %1 to %2").arg(QFileInfo(path).fileName(), QDir::toNativeSeparators(destination)), 5000);
}

bool MainWindow::relocate(const QString &path, const QString &target, const QString &title)
{
    const bool sameFolder = QFileInfo(path).absolutePath() == QFileInfo(target).absolutePath();
    const FileActions::Result result = sameFolder ? FileActions::rename(path, target, this)
                                                  : FileActions::move(path, target, this);
    if (!result.ok) {
        QMessageBox::warning(this, title,
                             (sameFolder ? tr("Cannot rename %1:\n%2") : tr("Cannot move %1:\n%2"))
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return false;
    }

    // Tabs of the file, or of files in the moved folder, follow it.
    for (EditorTab *tab : allTabs()) {
        if (FileActions::isSameOrInside(tab->filePath(), path))
            tab->setFilePath(result.path + tab->filePath().mid(QDir::cleanPath(path).size()));
    }
    if (FileActions::isSameOrInside(m_folderPanel->rootPath(), path))
        m_folderPanel->setRootPath(result.path + m_folderPanel->rootPath().mid(QDir::cleanPath(path).size()));
    if (RecentFiles::list().contains(path)) {
        RecentFiles::remove(path);
        RecentFiles::add(result.path);
    }
    Bookmarks::moved(path, result.path);
    updateWindowTitle();
    m_folderPanel->selectFile(result.path);
    return true;
}

void MainWindow::deletePath(const QString &path)
{
    const QFileInfo info(path);
    QList<EditorTab *> affected;
    bool unsaved = false;
    for (EditorTab *tab : allTabs()) {
        if (FileActions::isSameOrInside(tab->filePath(), path)) {
            affected << tab;
            unsaved = unsaved || tab->isModified();
        }
    }

    QString question = info.isDir() ? tr("Delete the folder “%1” and everything in it?").arg(info.fileName())
                                    : tr("Delete “%1”?").arg(info.fileName());
    question += u' ' + tr("It will be moved to the trash.");
    if (unsaved)
        question += u"\n\n" + tr("Unsaved changes in its open tabs will be lost.");
    QMessageBox box(QMessageBox::Question, tr("Delete"), question, QMessageBox::Cancel, this);
    QPushButton *remove = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != remove)
        return;

    FileActions::Result result = FileActions::moveToTrash(path, this);
    if (!result.ok) {
        QMessageBox failed(QMessageBox::Warning, tr("Delete"),
                           tr("Cannot move %1 to the trash:\n%2\n\nDelete it permanently instead? This can't be undone.")
                               .arg(QDir::toNativeSeparators(path), result.error),
                           QMessageBox::Cancel, this);
        QPushButton *permanent = failed.addButton(tr("Delete Permanently"), QMessageBox::DestructiveRole);
        failed.setDefaultButton(QMessageBox::Cancel);
        failed.exec();
        if (failed.clickedButton() != permanent)
            return;
        result = FileActions::removePermanently(path, this);
        if (!result.ok) {
            QMessageBox::warning(this, tr("Delete"),
                                 tr("Cannot delete %1:\n%2").arg(QDir::toNativeSeparators(path), result.error));
            return;
        }
    }
    for (EditorTab *tab : std::as_const(affected))
        discardTab(tab);
    RecentFiles::remove(path);
    Bookmarks::removed(path);
    statusBar()->showMessage(tr("Deleted %1").arg(info.fileName()), 5000);
}

void MainWindow::newNote(const QString &folder)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Note"), tr("Name:"), QLineEdit::Normal, tr("Untitled"), &ok)
                       .trimmed();
    if (!ok)
        return;
    if (const QString problem = FileActions::checkName(name); !problem.isEmpty()) {
        QMessageBox::warning(this, tr("New Note"), problem);
        return;
    }
    if (QFileInfo(name).suffix().isEmpty())
        name += QStringLiteral(".md"); // any other extension makes a text file of that type
    const FileActions::Result result = FileActions::createFile(QDir(folder).filePath(name), this);
    if (!result.ok) {
        QMessageBox::warning(this, tr("New Note"), tr("Cannot create %1:\n%2").arg(name, result.error));
        return;
    }
    openFile(result.path);
}

void MainWindow::newFolder(const QString &folder)
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New Folder"), tr("Name:"), QLineEdit::Normal, tr("New folder"), &ok).trimmed();
    if (!ok)
        return;
    if (const QString problem = FileActions::checkName(name); !problem.isEmpty()) {
        QMessageBox::warning(this, tr("New Folder"), problem);
        return;
    }
    const FileActions::Result result = FileActions::createFolder(QDir(folder).filePath(name), this);
    if (!result.ok) {
        QMessageBox::warning(this, tr("New Folder"), tr("Cannot create %1:\n%2").arg(name, result.error));
        return;
    }
    m_folderPanel->selectFile(result.path);
}

void MainWindow::duplicateFile(const QString &path)
{
    const FileActions::Result result = FileActions::duplicate(path, this);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Make a Copy"),
                             tr("Cannot copy %1:\n%2").arg(QDir::toNativeSeparators(path), result.error));
        return;
    }
    openFile(result.path);
}
