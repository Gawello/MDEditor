#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// Obsidian's search syntax: words (all must be there), "exact phrases", -excluded, OR between alternatives, and
// operators: path:, file:, tag:, content:, line:(…), section:(…), block:(…), task:(…), task-todo:(…),
// task-done:(…), match-case:, ignore-case:. Parentheses group terms.
class SearchQuery {
public:
    enum class Kind { Text, Path, File, Tag, Content, Line, Section, Block, Task, TaskTodo, TaskDone, Group };
    struct Term {
        Kind kind = Kind::Text;
        QString text;                  // for the plain kinds
        bool negated = false;
        Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
        std::shared_ptr<SearchQuery> inner; // line:(…), section:(…), …, (…)
    };

    static SearchQuery parse(const QString &text, Qt::CaseSensitivity defaultCase = Qt::CaseInsensitive);
    // Whether `text` uses any of the syntax beyond plain words (so a simple search does as before).
    static bool hasOperators(const QString &text);

    // A file matches: `relativePath` from the open folder, its whole `text`.
    bool matches(const QString &relativePath, const QString &text) const;
    // The lines worth showing for a match: those with a positive text term (0-based), with the first hit.
    struct Hit {
        int line = 0;
        int column = 0;
        int length = 0;
    };
    QList<Hit> hits(const QString &text) const;

    bool isEmpty() const { return m_alternatives.isEmpty(); }
    const QList<QList<Term>> &alternatives() const { return m_alternatives; } // OR of ANDs

private:
    bool matchTerms(const QList<Term> &terms, const QString &relativePath, const QString &text) const;
    bool scopeMatches(const QString &scope) const; // all terms of every alternative against one piece of text
    QList<QList<Term>> m_alternatives;
};
