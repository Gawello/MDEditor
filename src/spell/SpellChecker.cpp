#include "spell/SpellChecker.h"

#include "app/AppSettings.h"
#include "app/Background.h"
#include "document/Encodings.h"

#include <hunspell.hxx>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTextBoundaryFinder>
#include <QTextStream>

namespace {

// Hunspell opens files with fopen: on Windows only a "\\?\" path is taken as UTF-8.
std::string hunspellPath(const QString &path)
{
#ifdef Q_OS_WIN
    return (u"\\\\?\\" + QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())).toStdString();
#else
    return QFile::encodeName(path).toStdString();
#endif
}

QString userWordsFile()
{
    return QDir(SpellChecker::userFolder()).filePath(QStringLiteral("words.dic"));
}

bool hasLetter(const QString &word)
{
    return std::any_of(word.begin(), word.end(), [](QChar c) { return c.isLetter(); });
}

} // namespace

SpellChecker &SpellChecker::instance()
{
    static SpellChecker checker;
    return checker;
}

SpellChecker::SpellChecker()
{
    QFile words(userWordsFile());
    if (words.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&words);
        while (!in.atEnd()) {
            const QString word = in.readLine().trimmed();
            if (!word.isEmpty())
                m_userWords.insert(word);
        }
    }
    connect(&m_loading, &QFutureWatcher<LoadResult>::finished, this, &SpellChecker::loaded);
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(AppSettings::spellLanguagesKey))
            reload();
    });
}

QString SpellChecker::Dictionary::name() const
{
    const QLocale locale(code);
    if (locale.language() == QLocale::C)
        return code;
    return QStringLiteral("%1 (%2)").arg(QLocale::languageToString(locale.language()),
                                         QLocale::territoryToString(locale.territory()));
}

QString SpellChecker::userFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("dictionaries"));
}

QStringList SpellChecker::searchPaths()
{
    const QString app = QCoreApplication::applicationDirPath();
    QStringList paths{userFolder(), QDir(app).filePath(QStringLiteral("dictionaries")),
                      QDir(app).filePath(QStringLiteral("../share/mdeditor/dictionaries"))};
#ifndef Q_OS_WIN
    paths << QStringLiteral("/usr/share/hunspell") << QStringLiteral("/usr/share/myspell")
          << QStringLiteral("/usr/share/myspell/dicts") << QStringLiteral("/usr/local/share/hunspell");
#endif
    return paths;
}

QList<SpellChecker::Dictionary> SpellChecker::available()
{
    QList<Dictionary> result;
    QSet<QString> seen;
    for (const QString &path : searchPaths()) {
        const QDir dir(path);
        for (const QFileInfo &aff : dir.entryInfoList({QStringLiteral("*.aff")}, QDir::Files, QDir::Name)) {
            const QString code = aff.completeBaseName();
            const QString dic = dir.filePath(code + QStringLiteral(".dic"));
            if (seen.contains(code) || !QFileInfo::exists(dic))
                continue;
            seen.insert(code);
            result.append({code, aff.absoluteFilePath(), dic});
        }
    }
    return result;
}

QStringList SpellChecker::configuredLanguages()
{
    const QVariant stored = AppSettings::instance().value(QLatin1String(AppSettings::spellLanguagesKey));
    if (stored.isValid())
        return stored.toString().split(u',', Qt::SkipEmptyParts);

    // Default: the languages of the system's locales and US English. Regional settings come first: English
    // messages with Polish formats (LANG=en_US, LC_TIME=pl_PL) usually mean someone writing in Polish.
    QStringList codes;
    for (const char *variable : {"LC_ALL", "LC_CTYPE", "LC_TIME", "LC_NUMERIC", "LC_MONETARY", "LC_PAPER", "LANG"}) {
        const QString value = qEnvironmentVariable(variable).section(u'.', 0, 0);
        if (!value.isEmpty() && value != u"C" && value != u"POSIX")
            codes << value;
    }
    codes << QLocale::system().name() << QStringLiteral("en_US");
    // One dictionary per language: the exact one if there is ("en_US"), otherwise any of the language ("pl" finds
    // "pl_PL").
    QStringList result;
    QStringList languages;
    const QList<Dictionary> dictionaries = available();
    for (const QString &code : std::as_const(codes)) {
        const QString language = code.section(u'_', 0, 0);
        if (languages.contains(language))
            continue;
        const auto exact = std::find_if(dictionaries.begin(), dictionaries.end(),
                                        [&](const Dictionary &d) { return d.code == code; });
        const auto similar = std::find_if(dictionaries.begin(), dictionaries.end(),
                                          [&](const Dictionary &d) { return d.code.section(u'_', 0, 0) == language; });
        const auto chosen = exact != dictionaries.end() ? exact : similar;
        if (chosen != dictionaries.end()) {
            result << chosen->code;
            languages << language;
        }
    }
    return result;
}

void SpellChecker::reload(bool wait)
{
    const QStringList languages = configuredLanguages();
    // In the configured order: suggestions come from the first language first.
    std::vector<std::pair<QString, QString>> files;
    const QList<Dictionary> dictionaries = available();
    for (const QString &code : languages) {
        for (const Dictionary &dictionary : dictionaries) {
            if (dictionary.code == code)
                files.emplace_back(dictionary.aff, dictionary.dic);
        }
    }
    m_languages = languages;
    const int generation = ++m_generation;
    m_loading.setProperty("generation", generation);
    // Large dictionaries take a second to load: off the GUI thread. The objects are only used here afterwards.
    m_loading.setFuture(runInBackground([files]() {
        LoadResult result;
        for (const auto &[aff, dic] : files) {
            auto hunspell = std::make_shared<Hunspell>(hunspellPath(aff).c_str(), hunspellPath(dic).c_str());
            const QString encoding = QString::fromStdString(hunspell->get_dict_encoding());
            result.push_back({hunspell, Encodings::isSupported(encoding) ? encoding : Encodings::utf8});
        }
        return result;
    }));
    if (wait) {
        m_loading.waitForFinished();
        loaded();
    }
}

void SpellChecker::loaded()
{
    if (m_loading.property("generation").toInt() != m_generation || !m_loading.isFinished())
        return; // a newer reload is on its way
    m_dictionaries = m_loading.result();
    for (Loaded &dictionary : m_dictionaries) {
        for (const QString &word : std::as_const(m_userWords))
            dictionary.hunspell->add(Encodings::encode(word, dictionary.encoding, false).toStdString());
    }
    m_cache.clear();
    m_loading.setProperty("generation", -1); // handled
    emit changed();
}

bool SpellChecker::check(const QString &word)
{
    if (m_dictionaries.empty() || m_ignored.contains(word) || m_userWords.contains(word))
        return true;
    const auto cached = m_cache.constFind(word);
    if (cached != m_cache.constEnd())
        return *cached;

    // The typographic apostrophe is spelled ' in most dictionaries.
    QString plain = word;
    plain.replace(QChar(0x2019), u'\'');
    bool right = false;
    for (const Loaded &dictionary : m_dictionaries) {
        bool encodable = true;
        const QByteArray bytes = Encodings::encode(plain, dictionary.encoding, false, &encodable);
        if (encodable && dictionary.hunspell->spell(bytes.toStdString())) {
            right = true;
            break;
        }
    }
    if (m_cache.size() > 20000)
        m_cache.clear();
    m_cache.insert(word, right);
    return right;
}

QStringList SpellChecker::suggestions(const QString &word, int max)
{
    QStringList result;
    for (const Loaded &dictionary : m_dictionaries) {
        bool encodable = true;
        const QByteArray bytes = Encodings::encode(word, dictionary.encoding, false, &encodable);
        if (!encodable)
            continue;
        for (const std::string &suggestion : dictionary.hunspell->suggest(bytes.toStdString())) {
            const QString text = Encodings::decode(QByteArray::fromStdString(suggestion), dictionary.encoding);
            if (!result.contains(text))
                result << text;
        }
    }
    return result.mid(0, max);
}

void SpellChecker::addToDictionary(const QString &word)
{
    if (word.isEmpty() || m_userWords.contains(word))
        return;
    m_userWords.insert(word);
    for (Loaded &dictionary : m_dictionaries)
        dictionary.hunspell->add(Encodings::encode(word, dictionary.encoding, false).toStdString());
    m_cache.remove(word);

    QDir().mkpath(userFolder());
    QFile file(userWordsFile());
    if (file.open(QIODevice::Append | QIODevice::Text))
        file.write((word + u'\n').toUtf8());
    emit changed();
}

void SpellChecker::ignore(const QString &word)
{
    m_ignored.insert(word);
    emit changed();
}

std::vector<std::pair<int, int>> SpellChecker::misspelled(const QString &text, const std::function<bool(int)> &skip,
                                                         int caret)
{
    std::vector<std::pair<int, int>> result;
    if (m_dictionaries.empty())
        return result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);
    int start = 0;
    while (finder.toNextBoundary() >= 0) {
        const int end = int(finder.position());
        if (finder.boundaryReasons() & QTextBoundaryFinder::EndOfItem) {
            const QString word = text.mid(start, end - start);
            bool checkable = hasLetter(word) && !(caret >= start && caret <= end);
            for (int i = start; checkable && i < end; ++i)
                checkable = !text[i].isDigit() && !skip(i);
            // Acronyms and identifiers (HTML, snake_case) aren't words to correct.
            if (checkable && word.size() > 1 && word == word.toUpper())
                checkable = false;
            if (checkable && word.contains(u'_'))
                checkable = false;
            if (checkable && !check(word))
                result.emplace_back(start, end);
        }
        start = end;
    }
    return result;
}
