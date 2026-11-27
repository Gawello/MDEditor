#include "panels/GraphView.h"

#include "editor/EditorTheme.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"

#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QSet>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr qreal linkDistance = 70;
constexpr qreal repulsion = 2600;
constexpr qreal gravity = 0.015;
constexpr qreal damping = 0.6;
} // namespace

GraphView::GraphView(NoteIndex *index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
{
    setMouseTracking(true);
    setMinimumSize(300, 240);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        step();
        update();
    });
    connect(m_index, &NoteIndex::changed, this, &GraphView::rebuild);
    rebuild();
}

void GraphView::setCurrent(const QString &path)
{
    if (path == m_current)
        return;
    m_current = path;
    if (m_local)
        rebuild();
    else
        update();
}

void GraphView::setLocal(bool local)
{
    m_local = local;
    rebuild();
}

void GraphView::rebuild()
{
    // Keep where the notes already are, so a refresh doesn't scatter the layout.
    QHash<QString, QPointF> previous;
    for (const Node &node : m_nodes)
        previous.insert(node.path, node.position);

    QHash<QString, int> ids;
    std::vector<Node> nodes;
    QList<QString> paths;
    for (auto it = m_index->notes().cbegin(); it != m_index->notes().cend(); ++it)
        paths << it.key();
    paths.sort();
    for (const QString &path : std::as_const(paths)) {
        ids.insert(path, int(nodes.size()));
        Node node;
        node.path = path;
        node.name = QFileInfo(path).completeBaseName();
        nodes.push_back(node);
    }
    QSet<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> edges;
    for (const QString &path : std::as_const(paths)) {
        const auto &note = m_index->notes().value(path);
        for (const md::NoteLink &link : note->scan.links) {
            const QString target = m_index->resolve(QString::fromStdU16String(link.target), link.wiki, path);
            const int a = ids.value(path, -1);
            const int b = ids.value(target, -1);
            if (a < 0 || b < 0 || a == b)
                continue;
            const auto key = std::pair(std::min(a, b), std::max(a, b));
            if (seen.contains(key))
                continue;
            seen.insert(key);
            edges.push_back(key);
        }
    }

    if (m_local) { // the current note and its neighbors only
        const int center = ids.value(m_current, -1);
        QSet<int> keep;
        if (center >= 0) {
            keep.insert(center);
            for (const auto &[a, b] : edges) {
                if (a == center)
                    keep.insert(b);
                if (b == center)
                    keep.insert(a);
            }
        }
        std::vector<Node> kept;
        QHash<int, int> remap;
        for (int i = 0; i < int(nodes.size()); ++i) {
            if (keep.contains(i)) {
                remap.insert(i, int(kept.size()));
                kept.push_back(nodes[size_t(i)]);
            }
        }
        std::vector<std::pair<int, int>> keptEdges;
        for (const auto &[a, b] : edges) {
            if (remap.contains(a) && remap.contains(b))
                keptEdges.push_back({remap.value(a), remap.value(b)});
        }
        nodes = std::move(kept);
        edges = std::move(keptEdges);
    }

    QRandomGenerator random(42); // the same start for the same notes
    const qreal spread = 40 + 12 * std::sqrt(qreal(nodes.size()));
    for (Node &node : nodes) {
        const auto old = previous.constFind(node.path);
        node.position = old != previous.constEnd()
            ? *old
            : QPointF((random.generateDouble() - 0.5) * 2 * spread, (random.generateDouble() - 0.5) * 2 * spread);
    }
    m_neighbors.assign(nodes.size(), {});
    for (const auto &[a, b] : edges) {
        ++nodes[size_t(a)].degree;
        ++nodes[size_t(b)].degree;
        m_neighbors[size_t(a)].push_back(b);
        m_neighbors[size_t(b)].push_back(a);
    }
    m_nodes = std::move(nodes);
    m_edges = std::move(edges);
    m_hover = -1;
    m_dragged = -1;
    reheat();
}

void GraphView::reheat()
{
    m_alpha = 1;
    if (!m_nodes.empty())
        m_timer.start();
    update();
}

// Repulsion between all notes, springs along links, a pull to the center; velocities damped, and the whole
// cooling down until it stops.
void GraphView::step()
{
    const size_t n = m_nodes.size();
    std::vector<QPointF> force(n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            QPointF d = m_nodes[i].position - m_nodes[j].position;
            qreal distance2 = QPointF::dotProduct(d, d);
            if (distance2 < 0.01) {
                d = QPointF(0.1 * (int(i % 3) - 1), 0.1);
                distance2 = 0.02;
            }
            const QPointF push = d * (repulsion / (distance2 * std::sqrt(distance2)));
            force[i] += push;
            force[j] -= push;
        }
    }
    for (const auto &[a, b] : m_edges) {
        const QPointF d = m_nodes[size_t(b)].position - m_nodes[size_t(a)].position;
        const qreal distance = std::max<qreal>(1, std::hypot(d.x(), d.y()));
        const QPointF pull = d * (0.06 * (distance - linkDistance) / distance);
        force[size_t(a)] += pull;
        force[size_t(b)] -= pull;
    }
    qreal movement = 0;
    for (size_t i = 0; i < n; ++i) {
        Node &node = m_nodes[i];
        if (node.pinned) {
            node.velocity = {};
            continue;
        }
        force[i] -= node.position * gravity;
        node.velocity = (node.velocity + force[i] * m_alpha) * damping;
        const qreal speed = std::hypot(node.velocity.x(), node.velocity.y());
        if (speed > 30)
            node.velocity *= 30 / speed;
        node.position += node.velocity;
        movement += speed;
    }
    m_alpha *= 0.985;
    if (m_alpha < 0.02 || (n > 0 && movement / qreal(n) < 0.02 && m_alpha < 0.5))
        m_timer.stop();
}

qreal GraphView::radius(const Node &node) const
{
    return 3.5 + 1.6 * std::sqrt(qreal(node.degree));
}

QPointF GraphView::toScreen(const QPointF &p) const
{
    return QPointF(width() / 2.0, height() / 2.0) + m_pan + p * m_zoom;
}

QPointF GraphView::toWorld(const QPointF &p) const
{
    return (p - QPointF(width() / 2.0, height() / 2.0) - m_pan) / m_zoom;
}

int GraphView::nodeAt(const QPointF &screen) const
{
    int best = -1;
    qreal bestDistance = 1e9;
    for (int i = 0; i < int(m_nodes.size()); ++i) {
        const QPointF d = toScreen(m_nodes[size_t(i)].position) - screen;
        const qreal distance = std::hypot(d.x(), d.y());
        if (distance <= std::max<qreal>(6, radius(m_nodes[size_t(i)]) * m_zoom + 3) && distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void GraphView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette p = palette();
    const QColor text = p.color(QPalette::Text);
    const QColor base = p.color(QPalette::Base);
    const QColor accent = p.color(QPalette::Highlight);
    const QColor line = EditorTheme::mix(base, text, 0.22);
    const QColor dot = EditorTheme::mix(base, text, 0.55);

    if (m_nodes.empty()) {
        painter.setPen(EditorTheme::mix(base, text, 0.5));
        painter.drawText(rect(), Qt::AlignCenter,
                         m_index->root().isEmpty() ? tr("Open a folder to see how its notes link together.")
                         : m_local                 ? tr("The current note isn't in the open folder.")
                                                   : tr("No notes in this folder yet."));
        return;
    }

    const auto related = [this](int i) {
        if (m_hover < 0)
            return true;
        if (i == m_hover)
            return true;
        const auto &n = m_neighbors[size_t(m_hover)];
        return std::find(n.begin(), n.end(), i) != n.end();
    };

    for (const auto &[a, b] : m_edges) {
        const bool lit = m_hover >= 0 && (a == m_hover || b == m_hover);
        QColor color = lit ? accent : line;
        if (m_hover >= 0 && !lit)
            color.setAlphaF(0.3f);
        painter.setPen(QPen(color, lit ? 1.6 : 1));
        painter.drawLine(toScreen(m_nodes[size_t(a)].position), toScreen(m_nodes[size_t(b)].position));
    }

    QFont font = this->font();
    font.setPointSizeF(font.pointSizeF() * std::clamp(m_zoom, 0.7, 1.3));
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    for (int i = 0; i < int(m_nodes.size()); ++i) {
        const Node &node = m_nodes[size_t(i)];
        const QPointF center = toScreen(node.position);
        const bool current = node.path == m_current;
        QColor fill = current || i == m_hover ? accent : dot;
        if (!related(i))
            fill.setAlphaF(0.25f);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        const qreal r = radius(node) * std::clamp(m_zoom, 0.6, 2.0);
        painter.drawEllipse(center, r, r);
        // Names when zoomed in enough, or for what's hovered and its neighbors, and the current note.
        if (m_zoom > 0.9 || current || (m_hover >= 0 && related(i))) {
            QColor label = text;
            if (!related(i))
                label.setAlphaF(0.25f);
            painter.setPen(label);
            const qreal w = metrics.horizontalAdvance(node.name);
            painter.drawText(QPointF(center.x() - w / 2, center.y() + r + metrics.ascent() + 2), node.name);
        }
    }
}

void GraphView::mousePressEvent(QMouseEvent *event)
{
    m_pressPos = m_lastPos = event->position();
    m_moved = false;
    m_dragged = event->button() == Qt::LeftButton ? nodeAt(event->position()) : -1;
    m_panning = m_dragged < 0;
    if (m_dragged >= 0)
        m_nodes[size_t(m_dragged)].pinned = true;
}

void GraphView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (event->buttons() & (Qt::LeftButton | Qt::MiddleButton)) {
        if ((pos - m_pressPos).manhattanLength() > 3)
            m_moved = true;
        if (m_dragged >= 0) {
            m_nodes[size_t(m_dragged)].position = toWorld(pos);
            m_alpha = std::max<qreal>(m_alpha, 0.3);
            m_timer.start();
        } else if (m_panning) {
            m_pan += pos - m_lastPos;
        }
        m_lastPos = pos;
        update();
        return;
    }
    const int hover = nodeAt(pos);
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(hover >= 0 ? m_nodes[size_t(hover)].path : QString());
        update();
    }
}

void GraphView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragged >= 0) {
        m_nodes[size_t(m_dragged)].pinned = false;
        if (!m_moved && event->button() == Qt::LeftButton)
            emit noteActivated(m_nodes[size_t(m_dragged)].path);
    }
    m_dragged = -1;
    m_panning = false;
}

void GraphView::wheelEvent(QWheelEvent *event)
{
    const QPointF anchor = toWorld(event->position());
    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    m_zoom = std::clamp(m_zoom * factor, 0.15, 5.0);
    m_pan += event->position() - toScreen(anchor); // zoom around the mouse
    update();
}

void GraphView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}
