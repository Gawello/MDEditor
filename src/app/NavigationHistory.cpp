#include "app/NavigationHistory.h"

void NavigationHistory::visit(const Place &place, bool jump)
{
    if (place.path.isEmpty())
        return;
    if (m_index >= 0) {
        Place &current = m_places[m_index];
        if (current.path == place.path && (!jump || current.position == place.position)) {
            current.position = place.position;
            return;
        }
    }
    m_places.erase(m_places.begin() + (m_index + 1), m_places.end());
    m_places.append(place);
    if (m_places.size() > maxPlaces)
        m_places.removeFirst();
    m_index = int(m_places.size()) - 1;
}

void NavigationHistory::updatePosition(const QString &path, int position)
{
    if (m_index >= 0 && m_places[m_index].path == path)
        m_places[m_index].position = position;
}

std::optional<NavigationHistory::Place> NavigationHistory::back()
{
    if (!canGoBack())
        return std::nullopt;
    return m_places[--m_index];
}

std::optional<NavigationHistory::Place> NavigationHistory::forward()
{
    if (!canGoForward())
        return std::nullopt;
    return m_places[++m_index];
}

void NavigationHistory::remove(const QString &path)
{
    QList<Place> kept;
    int index = -1;
    const QString folder = path + u'/';
    for (int i = 0; i < m_places.size(); ++i) {
        if (m_places[i].path == path || m_places[i].path.startsWith(folder))
            continue;
        kept.append(m_places[i]);
        if (i <= m_index)
            index = int(kept.size()) - 1;
    }
    m_places = kept;
    m_index = m_places.isEmpty() ? -1 : std::max(index, 0);
    squeeze();
}

void NavigationHistory::rename(const QString &from, const QString &to)
{
    const QString folder = from + u'/';
    for (Place &place : m_places) {
        if (place.path == from)
            place.path = to;
        else if (place.path.startsWith(folder))
            place.path = to + u'/' + place.path.mid(folder.size());
    }
}

void NavigationHistory::squeeze()
{
    for (int i = int(m_places.size()) - 1; i > 0; --i) {
        if (m_places[i] != m_places[i - 1])
            continue;
        // Keep the entry the index is on.
        const int drop = (i == m_index) ? i - 1 : i;
        m_places.removeAt(drop);
        if (drop < m_index)
            --m_index;
    }
}
