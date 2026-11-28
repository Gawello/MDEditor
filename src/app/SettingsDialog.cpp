#include "app/SettingsDialog.h"

#include "app/CloudDrivePage.h"

#include "app/AppSettings.h"
#include "document/Encodings.h"
#include "spell/SpellChecker.h"
#include "format/FormatterRegistry.h"
#include "notes/Templates.h"
#include "plugins/PluginManager.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
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

SettingsDialog::SettingsDialog(CloudDrive *drive, QWidget *parent)
    : QDialog(parent)
    , m_sections(new QListWidget(this))
    , m_pages(new QStackedWidget(this))
{
    setWindowTitle(tr("Settings"));
    resize(760, 520);

    m_sections->setFixedWidth(170);
    m_sections->setFrameShape(QFrame::NoFrame);
    m_sections->setStyleSheet(QStringLiteral("QListWidget { background: transparent; outline: 0; }"
                                             "QListWidget::item { padding: 6px 10px; border-radius: 5px; }"
                                             // Styling the item drops the style's own selection background, but
                                             // not the selected text color: white on white without this.
                                             "QListWidget::item:selected { background: palette(highlight);"
                                             " color: palette(highlighted-text); }"));
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    addPage(tr("Appearance"), createAppearancePage());
    addPage(tr("Editor"), createEditorPage());
    addPage(tr("Formatting"), createFormattingPage());
    addPage(tr("Notes"), createNotesPage());
    addPage(tr("Google Drive"), new CloudDrivePage(drive));
    addPage(tr("Plugins"), createPluginsPage());
    addPage(tr("Updates"), createUpdatesPage());
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

QWidget *SettingsDialog::createNotesPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading(tr("Daily notes")));
    auto *form = new QFormLayout;
    const auto field = [&](const char *key, const QString &value, const QString &placeholder) {
        auto *edit = new QLineEdit(value, page);
        edit->setPlaceholderText(placeholder);
        const QString name = QString::fromLatin1(key);
        connect(edit, &QLineEdit::editingFinished, this,
                [edit, name] { AppSettings::instance().setValue(name, edit->text().trimmed()); });
        return edit;
    };
    AppSettings &settings = AppSettings::instance();
    form->addRow(tr("Folder"), field(AppSettings::dailyFolderKey, settings.dailyFolder(), tr("the open folder")));
    QLineEdit *format = field(AppSettings::dailyFormatKey, settings.dailyFormat(), QStringLiteral("YYYY-MM-DD"));
    auto *preview = new QLabel(page);
    preview->setForegroundRole(QPalette::PlaceholderText);
    const auto updatePreview = [format, preview] {
        const QString pattern = format->text().trimmed().isEmpty() ? QStringLiteral("YYYY-MM-DD") : format->text();
        preview->setText(tr("Today: %1.md").arg(Templates::formatDate(QDateTime::currentDateTime(), pattern)));
    };
    connect(format, &QLineEdit::textChanged, this, updatePreview);
    updatePreview();
    form->addRow(tr("Date format"), format);
    form->addRow(QString(), preview);
    form->addRow(tr("Template"), field(AppSettings::dailyTemplateKey, settings.dailyTemplate(),
                                       tr("none, e.g. templates/Daily.md")));
    layout->addLayout(form);
    layout->addWidget(hint(tr("File → Open Today's Note (Ctrl+Shift+D) opens the note for today, creating it from the "
                              "template if needed. Formats use Obsidian's tokens: YYYY, MM, DD, dddd, HH, mm; text in "
                              "[brackets] is kept as is.")));

    layout->addWidget(heading(tr("Links")));
    auto *links = new QFormLayout;
    auto *updateLinks = new QComboBox(page);
    updateLinks->addItem(tr("Ask"), QStringLiteral("ask"));
    updateLinks->addItem(tr("Always"), QStringLiteral("always"));
    updateLinks->addItem(tr("Never"), QStringLiteral("never"));
    updateLinks->setCurrentIndex(std::max(0, updateLinks->findData(settings.updateLinks())));
    connect(updateLinks, &QComboBox::currentIndexChanged, this, [updateLinks] {
        AppSettings::instance().setValue(QString::fromLatin1(AppSettings::updateLinksKey), updateLinks->currentData());
    });
    links->addRow(tr("Update links after a rename or move"), updateLinks);
    layout->addLayout(links);
    layout->addWidget(hint(tr("When a note or folder in the open folder is renamed or moved, the [[links]] and "
                              "Markdown links pointing to it, and the relative links of the notes it holds, can be "
                              "changed to match.")));

    layout->addWidget(heading(tr("Version history")));
    auto *history = new QFormLayout;
    auto *keep = new QCheckBox(tr("Keep copies of notes as they are opened and saved"), page);
    keep->setChecked(settings.snapshots());
    connect(keep, &QCheckBox::toggled, this,
            [](bool on) { AppSettings::instance().setValue(QString::fromLatin1(AppSettings::snapshotsKey), on); });
    history->addRow(QString(), keep);
    auto *minutes = new QSpinBox(page);
    minutes->setRange(0, 240);
    minutes->setSuffix(tr(" min"));
    minutes->setValue(settings.snapshotMinutes());
    connect(minutes, &QSpinBox::valueChanged, this,
            [](int value) { AppSettings::instance().setValue(QString::fromLatin1(AppSettings::snapshotMinutesKey), value); });
    history->addRow(tr("At most one copy every"), minutes);
    auto *days = new QSpinBox(page);
    days->setRange(1, 365);
    days->setSuffix(tr(" days"));
    days->setValue(settings.snapshotDays());
    connect(days, &QSpinBox::valueChanged, this,
            [](int value) { AppSettings::instance().setValue(QString::fromLatin1(AppSettings::snapshotDaysKey), value); });
    history->addRow(tr("Keep them for"), days);
    layout->addLayout(history);
    layout->addWidget(hint(tr("File → Version History shows a note's copies and can bring one back. They're kept in "
                              "MDEditor's own data folder, not next to the notes.")));

    layout->addWidget(heading(tr("Templates")));
    auto *templates = new QFormLayout;
    templates->addRow(tr("Templates folder"),
                      field(AppSettings::templatesFolderKey, settings.templatesFolder(), QStringLiteral("templates")));
    layout->addLayout(templates);
    layout->addWidget(hint(tr("Format → Insert → Template… inserts a note from this folder at the cursor. "
                              "{{title}}, {{date}} and {{time}} are filled in, also with a format: "
                              "{{date:dddd, D MMMM YYYY}}. Folders are relative to the open folder.")));
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createUpdatesPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading(tr("Updates")));
    layout->addWidget(new QLabel(tr("This is MDEditor %1.").arg(QApplication::applicationVersion()), page));

    auto *onStart = new QCheckBox(tr("Check for updates when MDEditor starts"), page);
    onStart->setChecked(AppSettings::instance().checkUpdatesOnStart());
    connect(onStart, &QCheckBox::toggled, this,
            [](bool checked) { AppSettings::instance().setValue(AppSettings::checkUpdatesOnStartKey, checked); });
    layout->addWidget(onStart);

    auto *now = new QPushButton(tr("Check Now"), page);
    connect(now, &QPushButton::clicked, this, &SettingsDialog::checkForUpdatesRequested);
    auto *row = new QHBoxLayout;
    row->addWidget(now);
    row->addStretch();
    layout->addLayout(row);
    layout->addWidget(hint(tr("A check asks GitHub (api.github.com) for the latest MDEditor release; nothing about "
                              "you or your files is sent. At startup it runs at most once a day and only speaks up "
                              "when there's a new version. The Windows setup and the AppImage can install the "
                              "update themselves; the .deb and .zip link to the download.")));
    layout->addStretch();
    return page;
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

    auto *encoding = new QComboBox(page);
    for (const QString &name : Encodings::common()) {
        if (!name.startsWith(u"UTF"))
            encoding->addItem(Encodings::label(name, false), name);
    }
    encoding->setCurrentIndex(encoding->findData(AppSettings::instance().fallbackEncoding()));
    connect(encoding, &QComboBox::currentIndexChanged, this, [encoding] {
        AppSettings::instance().setValue(AppSettings::fallbackEncodingKey, encoding->currentData());
    });
    form->addRow(tr("Encoding of other files"), encoding);

    const auto toggle = [&](const QString &text, const char *key, bool on) {
        auto *box = new QCheckBox(text, page);
        box->setChecked(on);
        const QString name = QString::fromLatin1(key);
        connect(box, &QCheckBox::toggled, this, [name](bool checked) { AppSettings::instance().setValue(name, checked); });
        return box;
    };
    form->addRow(QString(), toggle(tr("Show images from the web (they are downloaded when shown)"),
                                   AppSettings::remoteImagesKey, AppSettings::instance().remoteImages()));
    form->addRow(QString(), toggle(tr("Show the note's name as a title above it (edit it to rename the file)"),
                                   AppSettings::inlineTitleKey, AppSettings::instance().inlineTitle()));
    form->addRow(QString(), toggle(tr("Show line numbers"), AppSettings::lineNumbersKey, AppSettings::instance().lineNumbers()));
    form->addRow(QString(), toggle(tr("Close brackets and quotes as they are typed"), AppSettings::autoPairBracketsKey,
                                   AppSettings::instance().autoPairBrackets()));
    form->addRow(QString(), toggle(tr("Close `code` and wrap a selection in **, ==, ~, $ as they are typed"),
                                   AppSettings::autoPairMarkdownKey, AppSettings::instance().autoPairMarkdown()));
    form->addRow(QString(), toggle(tr("Turn formatted text pasted from web pages and documents into Markdown "
                                      "(Ctrl+Shift+V pastes plain text)"),
                                   AppSettings::convertPastedHtmlKey, AppSettings::instance().convertPastedHtml()));
    form->addRow(QString(), toggle(tr("Slash commands: “/” at the start of a line or after a space lists the commands"),
                                   AppSettings::slashCommandsKey, AppSettings::instance().slashCommands()));

    layout->addLayout(form);
    layout->addWidget(hint(tr("Pasted images are saved in this folder, next to the note.")));
    layout->addWidget(hint(tr("Files are read as UTF-8 when they are valid UTF-8 or start with a byte order mark, "
                              "and in the encoding above otherwise. They are saved in the encoding they were read "
                              "in; the status bar shows it and can change it.")));

    layout->addWidget(heading(tr("Spelling")));
    auto *languages = new QListWidget(page);
    const QStringList enabled = SpellChecker::configuredLanguages();
    for (const SpellChecker::Dictionary &dictionary : SpellChecker::available()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(dictionary.name(), dictionary.code), languages);
        item->setData(Qt::UserRole, dictionary.code);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(enabled.contains(dictionary.code) ? Qt::Checked : Qt::Unchecked);
    }
    languages->setMaximumHeight(160);
    connect(languages, &QListWidget::itemChanged, this, [languages] {
        QStringList codes;
        for (int i = 0; i < languages->count(); ++i) {
            if (languages->item(i)->checkState() == Qt::Checked)
                codes << languages->item(i)->data(Qt::UserRole).toString();
        }
        AppSettings::instance().setValue(AppSettings::spellLanguagesKey, codes.join(u',')); // empty: no checking
    });
    layout->addWidget(languages);
    auto *dictionaries = new QPushButton(tr("Open dictionaries folder"), page);
    connect(dictionaries, &QPushButton::clicked, this, [] {
        QDir().mkpath(SpellChecker::userFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(SpellChecker::userFolder()));
    });
    auto *row = new QHBoxLayout;
    row->addWidget(dictionaries);
    row->addStretch();
    layout->addLayout(row);
    layout->addWidget(hint(tr("Words are checked in every language ticked. More Hunspell dictionaries (.aff and .dic "
                              "files, e.g. from LibreOffice) can be put in the dictionaries folder; the words you "
                              "add are kept there in words.dic.")));
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createFormattingPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading(tr("Formatting")));
    layout->addWidget(hint(tr("Prettify (Shift+Alt+F) works on the selection, the code block or table under the "
                              "cursor; Prettify Document handles every code block and table. Indentation comes "
                              "from the Editor page.")));

    auto *onSave = new QCheckBox(tr("Prettify the document when saving"), page);
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
