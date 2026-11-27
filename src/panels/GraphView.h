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

    void setCurrent(const QString &path);
    void setLocal(bool local);
    void rebuild();

    int nodeCount() const { return int(m_nodes.size()); }
    int edgeCount() const { return int(m_edges.size()); }
    bool settled() const { return !m_timer.isActive(); }
    void step(); // one tick of the simulation (tests run it directly)

signals:
    void noteActivated(const QString &path);

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
    };
    qreal radius(const Node &node) const;
    QPointF toScreen(const QPointF &p) const;
    QPointF toWorld(const QPointF &p) const;
    int nodeAt(const QPointF &screen) const;
    void reheat();

    NoteIndex *m_index;
    QString m_current;
    bool m_local = false;
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
