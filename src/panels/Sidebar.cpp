#include "panels/Sidebar.h"

#include "app/AppSettings.h"
#include "editor/EditorTheme.h"
#include "panels/FavoritesPanel.h"
#include "panels/FolderPanel.h"

#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int minimumOpenHeight = 70;

} // namespace

namespace {

// The grip above the filler would be a handle on nothing. Hiding it doesn't last: the splitter shows it again
// whenever it recalculates.
class BlankHandle : public QSplitterHandle {
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void paintEvent(QPaintEvent *) override {}
};

class SidebarSplitter : public QSplitter {
public:
    using QSplitter::QSplitter;

protected:
    QSplitterHandle *createHandle() override
    {
        // One per widget, in order, and the filler is the fourth.
        if (++m_handles < 4)
            return QSplitter::createHandle();
        auto *handle = new BlankHandle(orientation(), this);
        handle->setEnabled(false);
        return handle;
    }

private:
    int m_handles = 0;
};

} // namespace

// A titled part of the sidebar that collapses to its header.
class SidebarSection : public QWidget {
public:
    SidebarSection(const QString &title, QWidget *content, QWidget *parent)
        : QWidget(parent)
        , m_header(new QToolButton(this))
        , m_content(content)
    {
        m_header->setText(title);
        m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_header->setAutoRaise(true);
        m_header->setArrowType(Qt::DownArrow);
        m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_header->setFocusPolicy(Qt::NoFocus);
        QFont font = m_header->font();
        font.setBold(true);
        m_header->setFont(font);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_header);
        layout->addWidget(content, 1);

        connect(m_header, &QToolButton::clicked, this, [this] { onHeaderClicked(); });
    }

    bool isOpen() const { return m_open; }
    int headerHeight() const { return m_header->sizeHint().height(); }

    std::function<void()> onHeaderClicked;
    int openHeight = 0; // to return to when reopened

    void setOpen(bool open)
    {
        m_open = open;
        m_header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        m_content->setVisible(open);
        // Closed, the splitter must not give it more than the header.
        setMaximumHeight(open ? QWIDGETSIZE_MAX : m_header->sizeHint().height());
    }

private:
    QToolButton *m_header;
    QWidget *m_content;
    bool m_open = true;
};

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent)
    , m_favorites(new FavoritesPanel(this))
    , m_vault(new FolderPanel(this))
    , m_files(new FolderPanel(this))
    , m_vaultStack(new QStackedWidget(this))
    , m_splitter(new SidebarSplitter(Qt::Vertical, this))
    , m_filler(new QWidget(this))
{
    auto *invitation = new QWidget(m_vaultStack);
    m_vaultHint = new QLabel(tr("A vault is a folder of linked notes. Without one, MDEditor is a plain Markdown editor."),
                             invitation);
    m_vaultHint->setWordWrap(true);
    auto *openVault = new QPushButton(tr("Open Vault…"), invitation);
    connect(openVault, &QPushButton::clicked, this, &Sidebar::openVaultRequested);
    auto *invitationLayout = new QVBoxLayout(invitation);
    invitationLayout->setContentsMargins(12, 6, 12, 6);
    invitationLayout->addWidget(m_vaultHint);
    invitationLayout->addWidget(openVault, 0, Qt::AlignLeft);
    invitationLayout->addStretch();
    m_vaultStack->addWidget(invitation);
    m_vaultStack->addWidget(m_vault);

    m_favoritesSection = new SidebarSection(tr("Favorites"), m_favorites, m_splitter);
    m_vaultSection = new SidebarSection(tr("Vault"), m_vaultStack, m_splitter);
    m_filesSection = new SidebarSection(tr("Files"), m_files, m_splitter);
    m_splitter->addWidget(m_favoritesSection);
    m_splitter->addWidget(m_vaultSection);
    m_splitter->addWidget(m_filesSection);
    m_splitter->addWidget(m_filler);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 1);
    m_splitter->setStretchFactor(3, 1);
    m_splitter->setSizes({130, 200, 300, 0});
    m_filler->hide();

    for (SidebarSection *section : {m_favoritesSection, m_vaultSection, m_filesSection})
        section->onHeaderClicked = [this, section] { toggle(section); };

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);
    applyStyle();

    connect(m_files, &FolderPanel::fileActivated, this, &Sidebar::fileActivated);
    connect(m_vault, &FolderPanel::fileActivated, this, &Sidebar::fileActivated);
    connect(m_files, &FolderPanel::openFolderRequested, this, &Sidebar::openFolderRequested);
    connect(m_vault, &FolderPanel::openFolderRequested, this, &Sidebar::openVaultRequested);
    connect(m_favorites, &FavoritesPanel::statusMessage, this, &Sidebar::statusMessage);
    connect(m_favorites, &FavoritesPanel::folderActivated, this, [this](const QString &path) {
        if (!m_filesSection->isOpen())
            toggle(m_filesSection);
        setRootPath(path);
    });
    for (FolderPanel *panel : {m_files, m_vault}) {
        connect(panel, &FolderPanel::addToFavoritesRequested, this, [this](const QString &path) {
            emit statusMessage(m_favorites->addFolder(path) ? tr("Added to favorites") : tr("Already in favorites"));
        });
    }
}

// Sizes are set explicitly, because left to itself the splitter hands freed space to the sections above and
// pushes a closed header to the bottom of the window. A closing section gives its space to the next open one
// below, or to the filler, so nothing above it moves.
void Sidebar::toggle(SidebarSection *section)
{
    const QList<SidebarSection *> sections{m_favoritesSection, m_vaultSection, m_filesSection};
    const int index = int(sections.indexOf(section));
    const int fillerIndex = int(sections.size());
    QList<int> sizes = m_splitter->sizes();
    const int headerHeight = section->headerHeight();
    // The filler's size here includes its handle. Left out, the splitter would make up the difference by
    // moving every section a little.
    const int handle = m_splitter->handleWidth();
    if (!m_filler->isHidden())
        sizes[fillerIndex] += handle;

    if (section->isOpen()) {
        section->openHeight = sizes[index];
        section->setOpen(false);
        int taker = fillerIndex;
        for (int i = index + 1; i < sections.size(); ++i) {
            if (sections[i]->isOpen()) {
                taker = i;
                break;
            }
        }
        sizes[taker] += sizes[index] - headerHeight;
        sizes[index] = headerHeight;
    } else {
        section->setOpen(true);
        const int wanted = std::max(section->openHeight, minimumOpenHeight);
        int needed = wanted - sizes[index];
        sizes[index] = wanted;
        // From the filler first, then from the open sections, the nearest below first.
        QList<int> givers{fillerIndex};
        for (int i = index + 1; i < sections.size(); ++i)
            givers << i;
        for (int i = index - 1; i >= 0; --i)
            givers << i;
        for (const int giver : givers) {
            if (giver != fillerIndex && !sections[giver]->isOpen())
                continue;
            const int floor = giver == fillerIndex ? 0 : minimumOpenHeight;
            const int taken = std::clamp(sizes[giver] - floor, 0, needed);
            sizes[giver] -= taken;
            needed -= taken;
        }
        sizes[index] -= needed; // what nobody could give
    }

    const bool fillerShown = sizes[fillerIndex] > handle;
    sizes[fillerIndex] = fillerShown ? sizes[fillerIndex] - handle : 0;
    m_filler->setVisible(fillerShown);
    m_splitter->setSizes(sizes);
}

void Sidebar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyStyle();
}

void Sidebar::applyStyle()
{
    const QPalette colors = palette();
    const QString style = QStringLiteral("color: %1;").arg(
        EditorTheme::mix(colors.color(QPalette::Window), colors.color(QPalette::WindowText), 0.6).name());
    if (m_vaultHint->styleSheet() != style) // setting it sends another PaletteChange
        m_vaultHint->setStyleSheet(style);
}

QString Sidebar::rootPath() const
{
    return m_files->rootPath();
}

void Sidebar::setRootPath(const QString &path)
{
    m_files->setRootPath(path);
}

void Sidebar::selectFile(const QString &path)
{
    m_files->selectFile(path);
    if (!m_vault->rootPath().isEmpty() && path.startsWith(m_vault->rootPath() + u'/'))
        m_vault->selectFile(path);
}

void Sidebar::setVaultRoot(const QString &path)
{
    if (path.isEmpty()) {
        m_vault->clearRoot();
        m_vaultStack->setCurrentIndex(0);
    } else {
        m_vault->setRootPath(path);
        m_vaultStack->setCurrentWidget(m_vault);
    }
}

void Sidebar::restoreLayout()
{
    AppSettings &settings = AppSettings::instance();
    m_splitter->restoreState(settings.value(AppSettings::sidebarSplitterKey).toByteArray());
    m_favoritesSection->setOpen(settings.value(AppSettings::sidebarFavoritesOpenKey, true).toBool());
    m_vaultSection->setOpen(settings.value(AppSettings::sidebarVaultOpenKey, true).toBool());
    m_filesSection->setOpen(settings.value(AppSettings::sidebarFilesOpenKey, true).toBool());
    m_filler->setVisible(!m_filesSection->isOpen());
}

void Sidebar::saveLayout() const
{
    AppSettings &settings = AppSettings::instance();
    settings.setValue(AppSettings::sidebarSplitterKey, m_splitter->saveState());
    settings.setValue(AppSettings::sidebarFavoritesOpenKey, m_favoritesSection->isOpen());
    settings.setValue(AppSettings::sidebarVaultOpenKey, m_vaultSection->isOpen());
    settings.setValue(AppSettings::sidebarFilesOpenKey, m_filesSection->isOpen());
}
