#include "app/SettingsDialog.h"

#include "app/AppSettings.h"
#include "format/FormatterRegistry.h"
#include "plugins/PluginManager.h"
#include "theme/ThemeManager.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Three dots showing a theme's background, text and accent colors.
QIcon swatch(const Theme &theme, const QPalette &fallback)
{
    QPixmap pixmap(48, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor colors[] = {
        theme.system ? fallback.color(QPalette::Base) : theme.base,
        theme.system ? fallback.color(QPalette::Text) : theme.text,
        theme.system ? fallback.color(QPalette::Highlight) : theme.accent,
    };
    for (int i = 0; i < 3; ++i) {
        painter.setPen(QPen(fallback.color(QPalette::Mid), 1));
        painter.setBrush(colors[i]);
        painter.drawEllipse(QRectF(2 + i * 15, 2, 12, 12));
    }
    return QIcon(pixmap);
}

QLabel *heading(const QString &text)
{
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 1.3);
    font.setBold(true);
    label->setFont(font);
    return label;
}

QLabel *hint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_sections(new QListWidget(this))
    , m_pages(new QStackedWidget(this))
{
    setWindowTitle(tr("Settings"));
    resize(760, 520);

    m_sections->setFixedWidth(170);
    m_sections->setFrameShape(QFrame::NoFrame);
    m_sections->setStyleSheet(QStringLiteral("QListWidget { background: transparent; outline: 0; }"
                                             "QListWidget::item { padding: 6px 10px; border-radius: 5px; }"));
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    addPage(tr("Appearance"), createAppearancePage());
    addPage(tr("Editor"), createEditorPage());
    addPage(tr("Formatting"), createFormattingPage());
    addPage(tr("Plugins"), createPluginsPage());
    m_sections->setCurrentRow(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *content = new QHBoxLayout;
    content->addWidget(m_sections);
    content->addWidget(m_pages, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);
}

void SettingsDialog::addPage(const QString &title, QWidget *page)
{
    auto *item = new QListWidgetItem(title, m_sections);
    item->setData(Qt::UserRole, title);
    m_pages->addWidget(page);
}

void SettingsDialog::showPage(const QString &title)
{
    for (int i = 0; i < m_sections->count(); ++i) {
        if (m_sections->item(i)->data(Qt::UserRole).toString() == title)
            m_sections->setCurrentRow(i);
    }
}

QWidget *SettingsDialog::createPluginsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading(tr("Plugins")));
    layout->addWidget(hint(tr("Plugins are JavaScript files that run inside MDEditor. Only enable plugins you "
                              "trust. Plugins can't access files or the network, only the open document.")));

    auto *list = new QListWidget(page);
    auto *details = new QPlainTextEdit(page);
    details->setReadOnly(true);
    details->setMaximumBlockCount(400);

    auto showDetails = [list, details] {
        details->clear();
        const QListWidgetItem *item = list->currentItem();
        if (!item)
            return;
        for (const PluginManager::Info &info : PluginManager::instance().plugins()) {
            if (info.id != item->data(Qt::UserRole).toString())
                continue;
            QStringList lines;
            lines << QStringLiteral("%1 %2").arg(info.name, info.version);
            if (!info.author.isEmpty())
                lines << tr("Author: %1").arg(info.author);
            if (!info.description.isEmpty())
                lines << info.description;
            lines << tr("Folder: %1").arg(QDir::toNativeSeparators(info.directory));
            lines << (info.loaded ? tr("Status: loaded") : info.enabled ? tr("Status: failed") : tr("Status: disabled"));
            if (!info.error.isEmpty())
                lines << tr("Error: %1").arg(info.error);
            if (!info.log.isEmpty())
                lines << QString() << tr("Log:") << info.log;
            details->setPlainText(lines.join(u'\n'));
        }
    };

    auto populate = [list, showDetails] {
        const QString current = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
        const QSignalBlocker blocker(list);
        list->clear();
        for (const PluginManager::Info &info : PluginManager::instance().plugins()) {
            QString label = info.version.isEmpty() ? info.name : QStringLiteral("%1  %2").arg(info.name, info.version);
            if (!info.error.isEmpty())
                label += tr("  (error)");
            auto *item = new QListWidgetItem(label, list);
            item->setData(Qt::UserRole, info.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(info.enabled ? Qt::Checked : Qt::Unchecked);
            if (info.id == current)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count() > 0)
            list->setCurrentRow(0);
        showDetails();
    };
    populate();
    connect(&PluginManager::instance(), &PluginManager::pluginsChanged, list, populate);
    connect(list, &QListWidget::currentItemChanged, list, showDetails);
    connect(list, &QListWidget::itemChanged, this, [](QListWidgetItem *item) {
        // Deferred: re-loading rebuilds this list, which must not happen inside its own signal.
        const QString id = item->data(Qt::UserRole).toString();
        const bool enabled = item->checkState() == Qt::Checked;
        QMetaObject::invokeMethod(&PluginManager::instance(),
                                  [id, enabled] { PluginManager::instance().setEnabled(id, enabled); },
                                  Qt::QueuedConnection);
    });

    auto *create = new QPushButton(tr("Create plugin…"), page);
    connect(create, &QPushButton::clicked, this, [this] {
        const QString name = QInputDialog::getText(this, tr("Create Plugin"), tr("Plugin name:"));
        if (name.trimmed().isEmpty())
            return;
        QString error;
        const QString directory = PluginManager::instance().createPlugin(name, &error);
        if (directory.isEmpty()) {
            QMessageBox::warning(this, tr("Create Plugin"), error);
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    auto *openFolder = new QPushButton(tr("Open plugins folder"), page);
    connect(openFolder, &QPushButton::clicked, this, [] {
        const QString directory = PluginManager::instance().pluginsDirectory();
        QDir().mkpath(directory);
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    auto *reload = new QPushButton(tr("Reload"), page);
    connect(reload, &QPushButton::clicked, this, [] { PluginManager::instance().reload(); });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(create);
    buttons->addWidget(openFolder);
    buttons->addWidget(reload);
    buttons->addStretch();

    layout->addWidget(list, 1);
    layout->addWidget(details, 1);
    layout->addLayout(buttons);
    return page;
}

QWidget *SettingsDialog::createAppearancePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading(tr("Appearance")));

    // Theme list.
    auto *themes = new QListWidget(page);
    themes->setIconSize(QSize(48, 16));
    auto populate = [this, themes] {
        const QSignalBlocker blocker(themes);
        themes->clear();
        for (const Theme &theme : ThemeManager::instance().themes()) {
            auto *item = new QListWidgetItem(swatch(theme, palette()), theme.name, themes);
            item->setData(Qt::UserRole, theme.id);
            if (theme.id == AppSettings::instance().theme())
                themes->setCurrentItem(item);
        }
    };
    populate();
    connect(&ThemeManager::instance(), &ThemeManager::themesChanged, themes, populate);
    connect(themes, &QListWidget::currentItemChanged, this, [](QListWidgetItem *item) {
        if (item)
            AppSettings::instance().setValue(AppSettings::themeKey, item->data(Qt::UserRole).toString());
    });

    auto *openThemes = new QPushButton(tr("Open themes folder"), page);
    connect(openThemes, &QPushButton::clicked, this, [] {
        const QString directory = ThemeManager::instance().userThemesDirectory();
        QDir().mkpath(directory);
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    auto *reloadThemes = new QPushButton(tr("Reload themes"), page);
    connect(reloadThemes, &QPushButton::clicked, this, [] { ThemeManager::instance().reloadUserThemes(); });
    auto *themeButtons = new QHBoxLayout;
    themeButtons->addWidget(openThemes);
    themeButtons->addWidget(reloadThemes);
    themeButtons->addStretch();

    layout->addWidget(new QLabel(tr("Theme")));
    layout->addWidget(themes, 1);
    layout->addLayout(themeButtons);
    layout->addWidget(hint(tr("Add your own themes as JSON files in the themes folder (see docs/themes.md).")));

    // Editor font.
    auto *form = new QFormLayout;
    auto *family = new QFontComboBox(page);
    auto *useDefault = new QCheckBox(tr("System default"), page);
    const QString currentFamily = AppSettings::instance().fontFamily();
    useDefault->setChecked(currentFamily.isEmpty());
    family->setEnabled(!currentFamily.isEmpty());
    if (!currentFamily.isEmpty())
        family->setCurrentFont(QFont(currentFamily));
    connect(useDefault, &QCheckBox::toggled, this, [family](bool checked) {
        family->setEnabled(!checked);
        AppSettings::instance().setValue(AppSettings::fontFamilyKey, checked ? QString() : family->currentFont().family());
    });
    connect(family, &QFontComboBox::currentFontChanged, this, [useDefault](const QFont &font) {
        if (!useDefault->isChecked())
            AppSettings::instance().setValue(AppSettings::fontFamilyKey, font.family());
    });
    auto *familyRow = new QHBoxLayout;
    familyRow->addWidget(family, 1);
    familyRow->addWidget(useDefault);

    auto *size = new QDoubleSpinBox(page);
    size->setRange(8, 32);
    size->setSingleStep(0.5);
    size->setDecimals(1);
    size->setSuffix(tr(" pt"));
    size->setValue(AppSettings::instance().fontSize());
    connect(size, &QDoubleSpinBox::valueChanged, this,
            [](double value) { AppSettings::instance().setValue(AppSettings::fontSizeKey, value); });

    form->addRow(tr("Editor font"), familyRow);
    form->addRow(tr("Font size"), size);
    layout->addLayout(form);
    return page;
}

QWidget *SettingsDialog::createEditorPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading(tr("Editor")));

    auto *form = new QFormLayout;
    auto *width = new QSpinBox(page);
    width->setRange(0, 300);
    width->setSuffix(tr(" characters"));
    width->setSpecialValueText(tr("Full width"));
    width->setValue(AppSettings::instance().readableWidth());
    connect(width, &QSpinBox::valueChanged, this,
            [](int value) { AppSettings::instance().setValue(AppSettings::readableWidthKey, value); });
    form->addRow(tr("Readable line length"), width);

    auto *indent = new QSpinBox(page);
    indent->setRange(1, 8);
    indent->setSuffix(tr(" spaces"));
    indent->setValue(AppSettings::instance().indentSize());
    connect(indent, &QSpinBox::valueChanged, this,
            [](int value) { AppSettings::instance().setValue(AppSettings::indentSizeKey, value); });
    form->addRow(tr("Indentation"), indent);

    auto *attachments = new QLineEdit(AppSettings::instance().attachmentsFolder(), page);
    connect(attachments, &QLineEdit::editingFinished, this, [attachments] {
        const QString folder = attachments->text().trimmed();
        AppSettings::instance().setValue(AppSettings::attachmentsFolderKey,
                                         folder.isEmpty() ? QStringLiteral("assets") : folder);
    });
    form->addRow(tr("Pasted images folder"), attachments);

    layout->addLayout(form);
    layout->addWidget(hint(tr("Pasted images are saved in this folder, next to the note.")));
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createFormattingPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading(tr("Formatting")));
    layout->addWidget(hint(tr("Format (Shift+Alt+F) works on the selection, the code block or table under the "
                              "cursor; Format Document handles every code block and table. Indentation comes "
                              "from the Editor page.")));

    auto *onSave = new QCheckBox(tr("Format the document when saving"), page);
    onSave->setChecked(AppSettings::instance().formatOnSave());
    connect(onSave, &QCheckBox::toggled, this,
            [](bool checked) { AppSettings::instance().setValue(AppSettings::formatOnSaveKey, checked); });
    layout->addWidget(onSave);

    layout->addWidget(new QLabel(tr("External formatters")));
    auto *form = new QFormLayout;
    const FormatterRegistry &registry = FormatterRegistry::instance();
    for (const QString &language : registry.builtinLanguages()) {
        auto *command = new QLineEdit(AppSettings::instance().formatterCommand(language), page);
        const QString detected = registry.detectedCommand(language);
        command->setPlaceholderText(detected.isEmpty() ? tr("built-in") : tr("auto: %1").arg(detected));
        connect(command, &QLineEdit::editingFinished, this, [command, language] {
            AppSettings::instance().setFormatterCommand(language, command->text().trimmed());
        });
        form->addRow(language, command);
    }
    layout->addLayout(form);
    layout->addWidget(hint(tr("Leave a field empty to use an installed tool (clang-format, ruff, black, prettier, "
                              "gofmt) or the built-in formatter. Type \"builtin\" to always use the built-in one, "
                              "or any command that reads code on stdin and writes the result to stdout.")));
    layout->addStretch();
    return page;
}
