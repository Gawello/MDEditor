#pragma once

#include <QList>
#include <QString>

#include <optional>

// Back / Forward through the places visited in the window, across tabs (Obsidian keeps one per tab; with
// MDEditor's "one tab per file" a window-wide list is what Back means). A place is a file and a cursor
// position; moving around inside a file only updates the current place, a jump (link, search result, outline,
// another tab) adds one.
class NavigationHistory {
public:
    struct Place {
        QString path;
        int position = 0;
        bool operator==(const Place &) const = default;
    };

    static constexpr int maxPlaces = 100;

    // We are now at `place`: a new entry after the current one (dropping the forward entries), or just the
    // current entry's position when it's the same file and nothing was jumped to.
    void visit(const Place &place, bool jump = false);
    // The cursor moved inside the current place's file.
    void updatePosition(const QString &path, int position);

    bool canGoBack() const { return m_index > 0; }
    bool canGoForward() const { return m_index + 1 < m_places.size(); }
    // The place to show; the caller opens it without calling visit().
    std::optional<Place> back();
    std::optional<Place> forward();
    // Drops the entries of a file (or of everything in a folder) that can't be shown any more; the current
    // entry becomes the nearest one before it.
    void remove(const QString &path);
    // A file or folder was renamed or moved: entries in it follow.
    void rename(const QString &from, const QString &to);

    const QList<Place> &places() const { return m_places; }
    int index() const { return m_index; }

private:
    void squeeze(); // merges neighbours that became the same place after a removal

    QList<Place> m_places;
    int m_index = -1;
};
