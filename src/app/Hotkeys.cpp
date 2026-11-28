#include "app/Hotkeys.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>

namespace {

QString settingKey(const QString &id)
{
    return QStringLiteral("hotkeys/") + QString(id).replace(u'/', u'|'); // '/' would make a settings group
}

QString clean(QString text)
{
    text = text.section(u'\t', 0, 0);
    text.remove(u'&');
    text.remove(QChar(0x2026)); // …
    return text.trimmed();
}

} // namespace

Hotkeys &Hotkeys::instance()
{
    static Hotkeys hotkeys;
    return hotkeys;
}

void Hotkeys::registerMenus(QMenuBar *menuBar)
{
    m_entries.clear();
    std::function<void(QMenu *, const QString &, const QString &)> walk = [&](QMenu *menu, const QString &id,
                                                                            const QString &shown) {
        for (QAction *action : menu->actions()) {
            if (action->isSeparator())
                continue;
            const QString name = clean(action->text());
            if (QMenu *sub = action->menu()) {
                walk(sub, id + u'/' + name, shown + QStringLiteral(" › ") + name);
                continue;
            }
            if (name.isEmpty())
                continue;
            m_entries.append({id + u'/' + name, name, shown, action->shortcuts(), action});
            apply(m_entries.back());
        }
    };
    for (QAction *top : menuBar->actions()) {
        QMenu *menu = top->menu();
        const QString name = clean(top->text());
        if (menu && name != u"Plugins")
            walk(menu, name, name);
    }
}

const Hotkeys::Entry *Hotkeys::find(const QString &id) const
{
    for (const Entry &entry : m_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

bool Hotkeys::isCustomized(const QString &id) const
{
    return QSettings().contains(settingKey(id));
}

QList<QKeySequence> Hotkeys::shortcuts(const QString &id) const
{
    const Entry *entry = find(id);
    if (!entry)
        return {};
    if (!isCustomized(id))
        return entry->defaults;
    QList<QKeySequence> result;
    for (const QString &text : QSettings().value(settingKey(id)).toStringList()) {
        const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (!sequence.isEmpty())
            result << sequence;
    }
    return result;
}

void Hotkeys::apply(const Entry &entry)
{
    if (entry.action)
        entry.action->setShortcuts(shortcuts(entry.id));
}

void Hotkeys::setShortcuts(const QString &id, const QList<QKeySequence> &shortcuts)
{
    const Entry *entry = find(id);
    if (!entry)
        return;
    QStringList texts;
    for (const QKeySequence &sequence : shortcuts)
        texts << sequence.toString(QKeySequence::PortableText);
    QSettings settings;
    if (shortcuts == entry->defaults)
        settings.remove(settingKey(id));
    else
        settings.setValue(settingKey(id), texts.isEmpty() ? QStringList{QString()} : texts); // [""]: none
    apply(*entry);
    emit changed(id);
}

void Hotkeys::reset(const QString &id)
{
    QSettings().remove(settingKey(id));
    if (const Entry *entry = find(id))
        apply(*entry);
    emit changed(id);
}

QStringList Hotkeys::conflicts(const QString &id, const QKeySequence &sequence) const
{
    QStringList names;
    if (sequence.isEmpty())
        return names;
    for (const Entry &entry : m_entries) {
        if (entry.id != id && shortcuts(entry.id).contains(sequence))
            names << entry.menu + QStringLiteral(" › ") + entry.name;
    }
    return names;
}
