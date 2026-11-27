#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class Hunspell;

// Spell checking with Hunspell dictionaries. Several languages can be on at once: a word is right if any of
// them knows it. Dictionaries load in the background; until then (and with none) every word counts as right.
// GUI thread only.
class SpellChecker : public QObject {
    Q_OBJECT

public:
    static SpellChecker &instance();

    struct Dictionary {
        QString code; // "pl_PL"
        QString aff;
        QString dic;
        QString name() const; // "Polish (Poland)"
    };
    // Where dictionaries are looked for, in order: the user's folder, the ones bundled with the app, the system's.
    static QStringList searchPaths();
    static QString userFolder();
    static QList<Dictionary> available();
    // Languages from Settings; by default the system's language plus US English, as available.
    static QStringList configuredLanguages();

    bool isActive() const { return !m_dictionaries.empty(); }
    QStringList languages() const { return m_languages; }

    bool check(const QString &word);
    QStringList suggestions(const QString &word, int max = 6);
    void addToDictionary(const QString &word); // kept in the user's folder (words.dic)
    void ignore(const QString &word);          // until the app quits

    // The words of `text` that are misspelled, as [start, end) ranges. `skip(i)` excludes characters (code,
    // links...); the word touching `caret` (being typed) is left alone too.
    std::vector<std::pair<int, int>> misspelled(const QString &text, const std::function<bool(int)> &skip,
                                                int caret = -1);

    // Loads the configured dictionaries (in the background). `wait` blocks until they are loaded (tests).
    void reload(bool wait = false);

signals:
    void changed(); // dictionaries or words changed: re-check the text

private:
    SpellChecker();
    struct Loaded {
        std::shared_ptr<Hunspell> hunspell;
        QString encoding; // of the dictionary's words
    };
    using LoadResult = std::vector<Loaded>;
    void loaded();

    std::vector<Loaded> m_dictionaries;
    QStringList m_languages;
    QSet<QString> m_userWords;
    QSet<QString> m_ignored;
    QHash<QString, bool> m_cache;
    QFutureWatcher<LoadResult> m_loading;
    int m_generation = 0;
};
