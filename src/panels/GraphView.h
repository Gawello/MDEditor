#pragma once

#include <QHash>
#include <QPointF>
#include <QTimer>
#include <QWidget>

#include <vector>

class NoteIndex;

// Obsidian's graph view: notes as dots, links as lines, laid out by a small force simulation. Drag a dot
// to move it, drag the background to pan, wheel to zoom; hovering highlights a note's neighbors and a click
// opens it. With `local`, only the current note and the notes it links with (both ways).
class GraphView : public QWidget {
    Q_OBJECT

public:
    explicit GraphView(NoteIndex *index, QWidget *parent = nullptr);

    // Obsidian's graph settings: Filters, Display and Forces.
    struct Settings {
        QString filter;           // notes whose path holds it (words), or "tag:x"
        bool tags = false;        // tags as nodes, linked to their notes
        bool attachments = false; // linked files that aren't notes
        bool existingOnly = true; // links to notes not created yet are left out
        bool orphans = true;      // notes without links
        bool arrows = false;      // the direction of links
        int depth = 1;            // local graph: links away from the current note
        qreal centerForce = 1;    // multipliers of the simulation's forces
        qreal repelForce = 1;
        qreal linkForce = 1;
        qreal linkDistance = 1;
        bool operator==(const Settings &) const = default;
    };
    void setSettings(const Settings &settings);
    const Settings &settings() const { return m_settings; }

    void setCurrent(const QString &path);
    void setLocal(bool local);
    void rebuild();
    // What a node is, for tests and colors.
    enum class Kind { Note, Tag, Attachment, Missing };
    QStringList nodeNames() const;
    Kind kindOf(const QString &name) const;

    int nodeCount() const { return int(m_nodes.size()); }
    int edgeCount() const { return int(m_edges.size()); }
    bool settled() const { return !m_timer.isActive(); }
    void step(); // one tick of the simulation (tests run it directly)

signals:
    void noteActivated(const QString &path);
    void tagActivated(const QString &tag); // a tag's node (Settings: tags)

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Node {
        QString path;
        QString name;
        QPointF position;
        QPointF velocity;
        int degree = 0;
        bool pinned = false; // being dragged
        Kind kind = Kind::Note;
    };
    qreal radius(const Node &node) const;
    QPointF toScreen(const QPointF &p) const;
    QPointF toWorld(const QPointF &p) const;
    int nodeAt(const QPointF &screen) const;
    void reheat();

    NoteIndex *m_index;
    QString m_current;
    bool m_local = false;
    Settings m_settings;
    std::vector<Node> m_nodes;
    std::vector<std::pair<int, int>> m_edges;
    std::vector<std::vector<int>> m_neighbors;
    QTimer m_timer;
    qreal m_alpha = 1;  // the simulation's temperature: forces shrink as it cools
    qreal m_zoom = 1;
    QPointF m_pan;      // screen position of the world origin, relative to the center
    int m_hover = -1;
    int m_dragged = -1;
    bool m_panning = false;
    bool m_moved = false;
    QPointF m_pressPos;
    QPointF m_lastPos;
};
