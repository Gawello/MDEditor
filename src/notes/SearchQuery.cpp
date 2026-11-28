#include "notes/SearchQuery.h"

#include "editor/LivePreviewHighlighter.h" // toView
#include "markdown/NoteScan.h"

#include <QFileInfo>

namespace {

// Never destroyed: queries run on detached threads that may outlive main().
const QStringList &operators = *new QStringList{"path:", "file:", "tag:", "content:", "line:", "section:", "block:", "task:",
                                                "task-todo:", "task-done:", "match-case:", "ignore-case:"};

// Splits into tokens: words, "quoted phrases", (groups) kept whole with their prefix.
QStringList tokenize(const QString &text)
{
    QStringList tokens;
    QString current;
    int depth = 0;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == u'"' && depth == 0)
            quoted = !quoted;
        if (!quoted) {
            if (c == u'(')
                ++depth;
            else if (c == u')')
                depth = std::max(0, depth - 1);
        }
        if (c.isSpace() && !quoted && depth == 0) {
            if (!current.isEmpty())
                tokens << current;
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.isEmpty())
        tokens << current;
    return tokens;
}

QString unquote(QString value)
{
    if (value.size() >= 2 && value.startsWith(u'"') && value.endsWith(u'"'))
        value = value.mid(1, value.size() - 2);
    return value;
}

} // namespace

bool SearchQuery::hasOperators(const QString &text)
{
    for (const QString &token : tokenize(text)) {
        QString bare = token.startsWith(u'-') ? token.mid(1) : token;
        if (bare == u"OR" || bare.startsWith(u'"') || bare.startsWith(u'(') || (bare.startsWith(u'[') && bare.endsWith(u']') && !bare.startsWith(u"[[")))
            return true;
        if (token.startsWith(u'-') && token.size() > 1)
            return true;
        for (const QString &op : operators) {
            if (bare.startsWith(op, Qt::CaseInsensitive))
                return true;
        }
    }
    return false;
}

SearchQuery SearchQuery::parse(const QString &text, Qt::CaseSensitivity defaultCase)
{
    SearchQuery query;
    QList<Term> current;
    Qt::CaseSensitivity sensitivity = defaultCase;
    for (QString token : tokenize(text)) {
        if (token == u"OR") {
            if (!current.isEmpty())
                query.m_alternatives << current;
            current.clear();
            continue;
        }
        Term term;
        if (token.startsWith(u'-') && token.size() > 1) {
            term.negated = true;
            token = token.mid(1);
        }
        // [property] / [property:value]
        if (token.startsWith(u'[') && token.endsWith(u']') && !token.startsWith(QStringLiteral("[[")) && token.size() > 2) {
            const QString inside = token.mid(1, token.size() - 2);
            term.kind = Kind::Property;
            term.key = inside.section(u':', 0, 0).trimmed();
            term.text = unquote(inside.section(u':', 1).trimmed());
            term.caseSensitivity = sensitivity;
            if (!term.key.isEmpty())
                current << term;
            continue;
        }
        Kind kind = Kind::Text;
        static const QList<std::pair<QString, Kind>> &kinds = *new QList<std::pair<QString, Kind>>{
            {"path:", Kind::Path}, {"file:", Kind::File}, {"tag:", Kind::Tag}, {"content:", Kind::Content},
            {"line:", Kind::Line}, {"section:", Kind::Section}, {"block:", Kind::Block}, {"task-todo:", Kind::TaskTodo},
            {"task-done:", Kind::TaskDone}, {"task:", Kind::Task}};
        for (const auto &[prefix, which] : kinds) {
            if (token.startsWith(prefix, Qt::CaseInsensitive)) {
                kind = which;
                token = token.mid(prefix.size());
                break;
            }
        }
        if (token.startsWith(QStringLiteral("match-case:"), Qt::CaseInsensitive)) {
            sensitivity = Qt::CaseSensitive;
            token = token.mid(11);
            if (token.isEmpty())
                continue;
        } else if (token.startsWith(QStringLiteral("ignore-case:"), Qt::CaseInsensitive)) {
            sensitivity = Qt::CaseInsensitive;
            token = token.mid(12);
            if (token.isEmpty())
                continue;
        }
        term.kind = kind;
        term.caseSensitivity = sensitivity;
        if (token.startsWith(u'(') && token.endsWith(u')')) { // a group, for any kind
            term.inner = std::make_shared<SearchQuery>(parse(token.mid(1, token.size() - 2), sensitivity));
            if (kind == Kind::Text)
                term.kind = Kind::Group;
        } else {
            term.text = unquote(token);
            if (kind == Kind::Line || kind == Kind::Section || kind == Kind::Block || kind == Kind::Task
                || kind == Kind::TaskTodo || kind == Kind::TaskDone)
                term.inner = std::make_shared<SearchQuery>(parse(term.text.contains(u' ') ? u'"' + term.text + u'"' : term.text,
                                                                 sensitivity));
            if (kind == Kind::Tag && term.text.startsWith(u'#'))
                term.text = term.text.mid(1);
        }
        if (term.inner || !term.text.isEmpty())
            current << term;
    }
    if (!current.isEmpty())
        query.m_alternatives << current;
    return query;
}

bool SearchQuery::scopeMatches(const QString &scope) const
{
    for (const QList<Term> &terms : m_alternatives) {
        if (matchTerms(terms, QString(), scope))
            return true;
    }
    return false;
}

bool SearchQuery::matches(const QString &relativePath, const QString &text) const
{
    for (const QList<Term> &terms : m_alternatives) {
        if (matchTerms(terms, relativePath, text))
            return true;
    }
    return false;
}

bool SearchQuery::matchTerms(const QList<Term> &terms, const QString &relativePath, const QString &text) const
{
    QStringList lines;
    std::vector<md::BlockInfo> blocks;
    const auto ensureLines = [&] {
        if (!lines.isEmpty() || text.isEmpty())
            return;
        lines = text.split(u'\n');
        std::vector<std::u16string_view> views;
        for (const QString &line : std::as_const(lines))
            views.push_back(toView(line));
        blocks = md::parseBlocks(views);
    };
    for (const Term &term : terms) {
        bool found = false;
        switch (term.kind) {
        case Kind::Text:
        case Kind::Content:
            found = text.contains(term.text, term.caseSensitivity);
            break;
        case Kind::Path:
            found = relativePath.contains(term.text, term.caseSensitivity);
            break;
        case Kind::File:
            found = QFileInfo(relativePath).fileName().contains(term.text, term.caseSensitivity);
            break;
        case Kind::Tag: {
            const md::NoteScan scan = md::scanNote(toView(text));
            for (const std::u16string &tag : scan.tags) {
                const QString name = QString::fromStdU16String(tag);
                if (name.compare(term.text, Qt::CaseInsensitive) == 0 || name.startsWith(term.text + u'/', Qt::CaseInsensitive))
                    found = true;
            }
            break;
        }
        case Kind::Group:
            found = term.inner && term.inner->matches(relativePath, text);
            break;
        case Kind::Property: {
            const md::NoteScan scan = md::scanNote(toView(text));
            for (const md::Property &property : scan.properties) {
                if (QString::fromStdU16String(property.key).compare(term.key, Qt::CaseInsensitive) != 0)
                    continue;
                if (term.text.isEmpty()) {
                    found = true;
                    break;
                }
                for (const std::u16string &value : md::propertyList(property.value)) {
                    if (QString::fromStdU16String(value).contains(term.text, term.caseSensitivity))
                        found = true;
                }
            }
            break;
        }
        case Kind::Line:
        case Kind::Block:
        case Kind::Task:
        case Kind::TaskTodo:
        case Kind::TaskDone:
            ensureLines();
            for (int i = 0; i < lines.size() && !found; ++i) {
                if (term.kind == Kind::Task || term.kind == Kind::TaskTodo || term.kind == Kind::TaskDone) {
                    const md::BlockInfo &info = blocks[size_t(i)];
                    if (info.type != md::BlockType::TaskItem || (term.kind == Kind::TaskTodo && info.checked)
                        || (term.kind == Kind::TaskDone && !info.checked))
                        continue;
                }
                // A block: the line and the ones around it up to blank lines.
                QString scope = lines[i];
                if (term.kind == Kind::Block) {
                    int end = i;
                    while (end + 1 < lines.size() && !lines[end + 1].trimmed().isEmpty())
                        scope += u'\n' + lines[++end];
                    i = end;
                }
                found = term.inner && term.inner->scopeMatches(scope);
            }
            break;
        case Kind::Section: {
            ensureLines();
            QString scope;
            for (int i = 0; i <= lines.size() && !found; ++i) {
                const bool heading = i < lines.size() && blocks[size_t(i)].type == md::BlockType::Heading;
                if (i == lines.size() || heading) {
                    found = term.inner && !scope.isEmpty() && term.inner->scopeMatches(scope);
                    scope.clear();
                }
                if (i < lines.size())
                    scope += lines[i] + u'\n';
            }
            break;
        }
        }
        if (found == term.negated)
            return false;
    }
    return true;
}

QList<SearchQuery::Hit> SearchQuery::hits(const QString &text) const
{
    // The plain words and phrases (also inside operators), not negated.
    QList<std::pair<QString, Qt::CaseSensitivity>> words;
    std::function<void(const SearchQuery &)> collect = [&](const SearchQuery &query) {
        for (const QList<Term> &terms : query.m_alternatives) {
            for (const Term &term : terms) {
                if (term.negated)
                    continue;
                if ((term.kind == Kind::Text || term.kind == Kind::Content) && !term.text.isEmpty())
                    words.append({term.text, term.caseSensitivity});
                else if (term.kind == Kind::Tag)
                    words.append({u'#' + term.text, Qt::CaseInsensitive});
                else if (term.inner)
                    collect(*term.inner);
            }
        }
    };
    collect(*this);
    QList<Hit> result;
    const QStringList lines = text.split(u'\n');
    for (int i = 0; i < lines.size(); ++i) {
        for (const auto &[word, sensitivity] : words) {
            const qsizetype at = lines[i].indexOf(word, 0, sensitivity);
            if (at >= 0) {
                result.append({i, int(at), int(word.size())});
                break;
            }
        }
    }
    return result;
}
