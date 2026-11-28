#include "notes/Completions.h"

#include "app/PaletteDialog.h"
#include "editor/LivePreviewHighlighter.h" // toView
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <vector>

namespace Completions {

namespace {

struct Ranked {
    int score = 0;
    int order = 0;
    Suggestion suggestion;
};

// Best score first, then the order the candidates came in.
QList<Suggestion> best(QList<Ranked> ranked)
{
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.score > b.score; });
    QList<Suggestion> result;
    for (const Ranked &item : std::as_const(ranked)) {
        if (result.size() >= maxSuggestions)
            break;
        result << item.suggestion;
    }
    return result;
}

// Fuzzy match on the label (and, a little less, the detail); -1 when it doesn't match at all.
int match(const QString &query, const QString &label, const QString &detail = {})
{
    if (query.isEmpty())
        return 0;
    const int own = PaletteDialog::score(query, label);
    const int withDetail = detail.isEmpty() ? -1 : PaletteDialog::score(query, detail + u' ' + label);
    // Half the weight; a miss (-1) must stay a miss, and -1 / 2 is 0.
    return std::max(own, withDetail < 0 ? -1 : withDetail / 2);
}

md::NoteScan scanOf(const NoteIndex &index, const QString &path, const QString &from, const QString &text)
{
    if (path.isEmpty() || QDir::cleanPath(path) == QDir::cleanPath(from))
        return md::scanNote(toView(text));
    if (const auto note = index.notes().value(QDir::cleanPath(path)))
        return note->scan;
    return {};
}

} // namespace

QList<Suggestion> suggest(const NoteIndex &index, const md::CompletionContext &context, const QString &from,
                          const QString &text)
{
    const QString query = QString::fromStdU16String(context.query);
    QList<Ranked> ranked;
    const auto add = [&](int score, Suggestion suggestion) {
        if (score >= 0)
            ranked.append({score, int(ranked.size()), std::move(suggestion)});
    };

    switch (context.kind) {
    case md::CompletionKind::None:
    case md::CompletionKind::Command: // the window's
        return {};
    case md::CompletionKind::Note: {
        // Score everything, but build only the suggestions that will be shown: this runs on every key.
        struct Hit {
            int score;
            int order;
            int entry; // index in entries(), or -1 for an alias
            QString alias;
            QString aliasPath;
        };
        std::vector<Hit> hits;
        const QList<NoteIndex::FileEntry> &entries = index.entries();
        const QString self = QDir::cleanPath(from);
        int order = 0;
        // Notes before other files, each in the index's (alphabetical) order.
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < entries.size(); ++i) {
                const NoteIndex::FileEntry &entry = entries[i];
                if (entry.note != (pass == 0) || entry.path == self)
                    continue;
                if (query.isEmpty() && order >= maxSuggestions)
                    break; // nothing to rank
                if (const int score = match(query, entry.name, entry.folder); score >= 0)
                    hits.push_back({score, order++, i, {}, {}});
            }
        }
        for (const auto &note : index.notes()) {
            for (const QString &alias : NoteIndex::aliasesIn(note->scan)) {
                if (const int score = match(query, alias); score >= 0)
                    hits.push_back({score, order++, -1, alias, note->path});
            }
        }
        const auto better = [](const Hit &a, const Hit &b) { return a.score != b.score ? a.score > b.score : a.order < b.order; };
        const size_t shown = std::min<size_t>(hits.size(), maxSuggestions);
        std::partial_sort(hits.begin(), hits.begin() + qsizetype(shown), hits.end(), better);
        QList<Suggestion> result;
        for (size_t i = 0; i < shown; ++i) {
            const Hit &hit = hits[i];
            if (hit.entry >= 0) {
                const NoteIndex::FileEntry &entry = entries[hit.entry];
                result.append({entry.name, entry.folder, index.linkText(entry.path)});
            } else {
                result.append({hit.alias, QStringLiteral("→ ") + QFileInfo(hit.aliasPath).completeBaseName(),
                               index.linkText(hit.aliasPath) + u'|' + hit.alias});
            }
        }
        return result;
    }
    case md::CompletionKind::Heading: {
        const QString note = QString::fromStdU16String(context.note);
        const QString path = note.isEmpty() ? from : index.resolve(note, true, from);
        for (const md::Heading &heading : scanOf(index, path, from, text).headings) {
            const QString label = QString::fromStdU16String(heading.text);
            add(match(query, label), {label, QStringLiteral("H%1").arg(heading.level), label});
        }
        break;
    }
    case md::CompletionKind::Block: {
        const QString note = QString::fromStdU16String(context.note);
        const QString path = note.isEmpty() ? from : index.resolve(note, true, from);
        for (const md::BlockId &block : scanOf(index, path, from, text).blocks) {
            const QString id = QString::fromStdU16String(block.id);
            const QString label = QString::fromStdU16String(block.text);
            add(match(query, id, label), {label.isEmpty() ? id : label, u'^' + id, id});
        }
        break;
    }
    case md::CompletionKind::Tag: {
        const QHash<QString, int> counts = index.tagCounts();
        QStringList tags = counts.keys();
        // Without a query, the most used first.
        std::sort(tags.begin(), tags.end(), [&counts](const QString &a, const QString &b) {
            return counts.value(a) != counts.value(b) ? counts.value(a) > counts.value(b)
                                                      : a.compare(b, Qt::CaseInsensitive) < 0;
        });
        for (const QString &tag : std::as_const(tags)) {
            if (tag.compare(query, Qt::CaseInsensitive) == 0 && counts.value(tag) <= 1)
                continue; // only the tag being typed: nothing to suggest
            add(match(query, tag), {u'#' + tag, QString::number(counts.value(tag)), tag});
        }
        break;
    }
    }
    return best(ranked);
}

} // namespace Completions
