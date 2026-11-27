#include "panels/SearchPanel.h"

#include "app/Background.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"
#include "syntax/Syntax.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int pathRole = Qt::UserRole;
constexpr int lineRole = Qt::UserRole + 1;
constexpr int columnRole = Qt::UserRole + 2;
constexpr int lengthRole = Qt::UserRole + 3;
constexpr qint64 maxFileSize = 8 * 1024 * 1024;

bool searchable(const QString &path)
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    static const QStringList text{"txt", "log", "csv", "tsv", "rst", "adoc", "org", "tex", "bib"};
    return MarkdownEditor::isMarkdownFile(path) || text.contains(suffix)
        || syntax::languageForFileName(std::u16string_view(reinterpret_cast<const char16_t *>(info.fileName().utf16()),
                                                           size_t(info.fileName().size())))
        != 0;
}

} // namespace

SearchPanel::SearchPanel(NoteIndex *index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
    , m_field(new QLineEdit(this))
    , m_case(new QCheckBox(tr("Match case"), this))
    , m_regex(new QCheckBox(tr("Regex"), this))
    , m_status(new QLabel(this))
    , m_results(new QTreeWidget(this))
{
    m_field->setPlaceholderText(tr("Search in folder…"));
    m_field->setClearButtonEnabled(true);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_status->setWordWrap(true);
    m_results->setHeaderHidden(true);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setIndentation(12);
    m_results->setUniformRowHeights(true);

    auto *options = new QHBoxLayout;
    options->setContentsMargins(0, 0, 0, 0);
    options->addWidget(m_case);
    options->addWidget(m_regex);
    options->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 0);
    layout->addWidget(m_field);
    layout->addLayout(options);
    layout->addWidget(m_status);
    layout->addWidget(m_results, 1);

    connect(m_field, &QLineEdit::returnPressed, this, &SearchPanel::search);
    connect(m_case, &QCheckBox::toggled, this, &SearchPanel::search);
    connect(m_regex, &QCheckBox::toggled, this, &SearchPanel::search);
    connect(&m_watcher, &QFutureWatcher<QList<FileMatches>>::finished, this, [this] {
        const QList<FileMatches> results = m_watcher.result();
        int count = 0;
        for (const FileMatches &file : results)
            count += int(file.matches.size());
        showResults(results, count >= maxMatches);
    });
    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit openRequested(item->data(0, pathRole).toString(), item->data(0, lineRole).toInt(),
                           item->data(0, columnRole).toInt(), item->data(0, lengthRole).toInt());
    });
    connect(m_results, &QTreeWidget::itemClicked, m_results, &QTreeWidget::itemActivated);
}

SearchPanel::~SearchPanel()
{
    if (m_cancel)
        *m_cancel = true;
}

void SearchPanel::focusSearch(const QString &text)
{
    if (!text.isEmpty()) {
        m_field->setText(text);
        search();
    }
    m_field->setFocus();
    m_field->selectAll();
}

QList<SearchPanel::FileMatches> SearchPanel::run(const QStringList &files, const Query &query,
                                                const std::atomic<bool> *cancel)
{
    QList<FileMatches> results;
    QRegularExpression pattern;
    if (query.regex) {
        pattern.setPattern(query.text);
        if (!query.caseSensitive)
            pattern.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!pattern.isValid())
            return results;
    }
    const Qt::CaseSensitivity cs = query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int total = 0;
    for (const QString &path : files) {
        if ((cancel && *cancel) || total >= maxMatches)
            break;
        if (!searchable(path))
            continue;
        QFile file(path);
        if (file.size() > maxFileSize || !file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        if (bytes.left(65536).contains('\0'))
            continue; // binary after all
        const QString text = QString::fromUtf8(bytes);
        FileMatches found{path, {}};
        int lineNumber = 0;
        for (QStringView line : QStringView(text).split(u'\n')) {
            if (line.endsWith(u'\r'))
                line.chop(1);
            const auto add = [&](int column, int length) {
                found.matches.append({lineNumber, column, length, line.toString()});
                ++total;
            };
            if (query.regex) {
                auto it = pattern.globalMatchView(line);
                while (it.hasNext() && total < maxMatches) {
                    const QRegularExpressionMatch match = it.next();
                    if (match.capturedLength() > 0)
                        add(int(match.capturedStart()), int(match.capturedLength()));
                }
            } else {
                for (qsizetype at = line.indexOf(query.text, 0, cs); at >= 0 && total < maxMatches;
                     at = line.indexOf(query.text, at + query.text.size(), cs))
                    add(int(at), int(query.text.size()));
            }
            ++lineNumber;
        }
        if (!found.matches.isEmpty())
            results << found;
    }
    return results;
}

void SearchPanel::search()
{
    if (m_cancel)
        *m_cancel = true;
    m_results->clear();
    const Query query{m_field->text(), m_case->isChecked(), m_regex->isChecked()};
    if (query.text.isEmpty()) {
        m_status->clear();
        return;
    }
    if (m_index->root().isEmpty()) {
        m_status->setText(tr("Open a folder to search in it."));
        return;
    }
    if (query.regex && !QRegularExpression(query.text).isValid()) {
        m_status->setText(tr("Invalid regular expression: %1").arg(QRegularExpression(query.text).errorString()));
        return;
    }
    m_status->setText(tr("Searching…"));
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_cancel;
    const QStringList files = m_index->files();
    m_watcher.setFuture(runInBackground([files, query, cancel] { return run(files, query, cancel.get()); }));
}

void SearchPanel::showResults(const QList<FileMatches> &results, bool truncated)
{
    m_results->clear();
    const QDir root(m_index->root());
    int count = 0;
    for (const FileMatches &file : results) {
        auto *top = new QTreeWidgetItem(m_results);
        top->setText(0, QStringLiteral("%1  (%2)").arg(root.relativeFilePath(file.path)).arg(file.matches.size()));
        top->setToolTip(0, QDir::toNativeSeparators(file.path));
        top->setData(0, pathRole, file.path);
        top->setData(0, lineRole, file.matches.isEmpty() ? 0 : file.matches.first().line);
        QFont bold = top->font(0);
        bold.setBold(true);
        top->setFont(0, bold);
        for (const Match &match : file.matches) {
            auto *row = new QTreeWidgetItem(top);
            // A window around the match, so it's visible in a narrow panel.
            const int from = std::max(0, match.column - 24);
            QString snippet = match.text.mid(from, 120).trimmed();
            if (from > 0)
                snippet.prepend(u'…');
            row->setText(0, QStringLiteral("%1: %2").arg(match.line + 1).arg(snippet));
            row->setToolTip(0, match.text.left(400));
            row->setData(0, pathRole, file.path);
            row->setData(0, lineRole, match.line);
            row->setData(0, columnRole, match.column);
            row->setData(0, lengthRole, match.length);
            ++count;
        }
    }
    m_results->expandAll();
    if (results.isEmpty())
        m_status->setText(tr("No matches."));
    else if (truncated)
        m_status->setText(tr("First %1 matches in %2 files.").arg(count).arg(results.size()));
    else
        m_status->setText(tr("%n match(es)", nullptr, count) + u' ' + tr("in %n file(s).", nullptr, int(results.size())));
}

void SearchPanel::showTag(const QString &tag)
{
    if (m_cancel)
        *m_cancel = true;
    m_field->setText(u'#' + tag);
    QList<FileMatches> results;
    QStringList paths;
    for (const auto &note : m_index->notes()) {
        for (const std::u16string &name : note->scan.tags) {
            const QString value = QString::fromStdU16String(name);
            if (value.compare(tag, Qt::CaseInsensitive) == 0 || value.startsWith(tag + u'/', Qt::CaseInsensitive)) {
                paths << note->path;
                break;
            }
        }
    }
    paths.sort(Qt::CaseInsensitive);
    // The lines with the tag, found like a search (frontmatter tags give the file alone).
    const QString escaped = QRegularExpression::escape(tag);
    const Query query{QStringLiteral("(?<!\\S)#%1(?:/[^\\s]*)?(?![\\w/-])").arg(escaped), false, true};
    QList<FileMatches> lines = run(paths, query, nullptr);
    for (const QString &path : std::as_const(paths)) {
        auto it = std::find_if(lines.begin(), lines.end(), [&](const FileMatches &f) { return f.path == path; });
        results << (it != lines.end() ? *it : FileMatches{path, {}});
    }
    showResults(results, false);
    m_status->setText(tr("%n note(s) tagged #%1", nullptr, int(paths.size())).arg(tag));
}
