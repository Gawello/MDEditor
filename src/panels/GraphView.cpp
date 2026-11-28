#include "panels/GraphView.h"

#include "editor/EditorTheme.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"

#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
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

void GraphView::setSettings(const Settings &settings)
{
    if (settings == m_settings)
        return;
    const bool layoutOnly = settings.filter == m_settings.filter && settings.tags == m_settings.tags
                            && settings.attachments == m_settings.attachments
                            && settings.existingOnly == m_settings.existingOnly && settings.orphans == m_settings.orphans
                            && settings.depth == m_settings.depth;
    m_settings = settings;
    if (layoutOnly)
        reheat(); // forces or arrows
    else
        rebuild();
}

QStringList GraphView::nodeNames() const
{
    QStringList names;
    for (const Node &node : m_nodes)
        names << node.name;
    return names;
}

GraphView::Kind GraphView::kindOf(const QString &name) const
{
    for (const Node &node : m_nodes) {
        if (node.name == name)
            return node.kind;
    }
    return Kind::Note;
}

void GraphView::rebuild()
{
    // Keep where the notes already are, so a refresh doesn't scatter the layout.
    QHash<QString, QPointF> previous;
    for (const Node &node : m_nodes)
        previous.insert(node.path, node.position);

    // Nodes: the notes, then (as the settings say) tags, attachments and notes not created yet, which links add.
    QHash<QString, int> ids;
    std::vector<Node> nodes;
    const auto nodeFor = [&](const QString &key, const QString &name, Kind kind) {
        if (const auto it = ids.constFind(key); it != ids.constEnd())
            return *it;
        ids.insert(key, int(nodes.size()));
        Node node;
        node.path = key;
        node.name = name;
        node.kind = kind;
        nodes.push_back(node);
        return int(nodes.size()) - 1;
    };
    // The filter: words of the path, or tag:x.
    const QString filter = m_settings.filter.trimmed();
    const QString tagFilter = filter.startsWith(QStringLiteral("tag:"), Qt::CaseInsensitive) ? filter.mid(4).remove(u'#') : QString();
    const QStringList words = tagFilter.isEmpty() ? filter.split(u' ', Qt::SkipEmptyParts) : QStringList();
    const QDir root(m_index->root());
    QList<QString> paths;
    for (auto it = m_index->notes().cbegin(); it != m_index->notes().cend(); ++it) {
        const QString relative = root.relativeFilePath(it.key());
        bool keep = std::all_of(words.cbegin(), words.cend(),
                                [&relative](const QString &word) { return relative.contains(word, Qt::CaseInsensitive); });
        if (!tagFilter.isEmpty()) {
            keep = false;
            for (const std::u16string &tag : (*it)->scan.tags) {
                const QString name = QString::fromStdU16String(tag);
                keep = keep || name.compare(tagFilter, Qt::CaseInsensitive) == 0
                       || name.startsWith(tagFilter + u'/', Qt::CaseInsensitive);
            }
        }
        if (keep)
            paths << it.key();
    }
    paths.sort();
    for (const QString &path : std::as_const(paths))
        nodeFor(path, QFileInfo(path).completeBaseName(), Kind::Note);

    QSet<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> edges; // from, to
    const auto link = [&](int a, int b) {
        if (a < 0 || b < 0 || a == b)
            return;
        const auto key = std::pair(std::min(a, b), std::max(a, b));
        if (seen.contains(key))
            return;
        seen.insert(key);
        edges.push_back({a, b});
    };
    for (const QString &path : std::as_const(paths)) {
        const auto &note = m_index->notes().value(path);
        const int from = ids.value(path, -1);
        for (const md::NoteLink &noteLink : note->scan.links) {
            const QString written = QString::fromStdU16String(noteLink.target);
            const QString target = m_index->resolveIndexed(written, noteLink.wiki, path); // no disk access
            if (target.isEmpty()) {
                if (!m_settings.existingOnly && noteLink.wiki && !noteLink.embed)
                    link(from, nodeFor(u"missing:" + written.toLower(), QFileInfo(written).completeBaseName(), Kind::Missing));
                continue;
            }
            if (ids.contains(target)) {
                link(from, ids.value(target));
            } else if (m_settings.attachments && !MarkdownEditor::isMarkdownFile(target)) {
                link(from, nodeFor(target, QFileInfo(target).fileName(), Kind::Attachment));
            }
        }
        if (m_settings.tags) {
            for (const std::u16string &tag : note->scan.tags) {
                const QString name = QString::fromStdU16String(tag);
                link(from, nodeFor(u"tag:" + name.toLower(), u'#' + name, Kind::Tag));
            }
        }
    }

    // What stays: the local graph (links within `depth` steps of the current note), then orphans or not.
    QSet<int> keep;
    if (m_local) {
        const int center = ids.value(m_current, -1);
        if (center >= 0) {
            QSet<int> frontier{center};
            keep.insert(center);
            for (int step = 0; step < std::max(1, m_settings.depth); ++step) {
                QSet<int> next;
                for (const auto &[a, b] : edges) {
                    if (frontier.contains(a) && !keep.contains(b))
                        next.insert(b);
                    if (frontier.contains(b) && !keep.contains(a))
                        next.insert(a);
                }
                keep.unite(next);
                frontier = next;
            }
        }
    } else {
        for (int i = 0; i < int(nodes.size()); ++i)
            keep.insert(i);
    }
    if (!m_settings.orphans) {
        QSet<int> linked;
        for (const auto &[a, b] : edges) {
            if (keep.contains(a) && keep.contains(b)) {
                linked.insert(a);
                linked.insert(b);
            }
        }
        keep.intersect(linked);
        if (m_local && ids.contains(m_current))
            keep.insert(ids.value(m_current)); // the current note always shows
    }
    {
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
            const QPointF push = d * (repulsion * m_settings.repelForce / (distance2 * std::sqrt(distance2)));
            force[i] += push;
            force[j] -= push;
        }
    }
    for (const auto &[a, b] : m_edges) {
        const QPointF d = m_nodes[size_t(b)].position - m_nodes[size_t(a)].position;
        const qreal distance = std::max<qreal>(1, std::hypot(d.x(), d.y()));
        const QPointF pull = d * (0.06 * m_settings.linkForce * (distance - linkDistance * m_settings.linkDistance) / distance);
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
        force[i] -= node.position * (gravity * m_settings.centerForce);
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
        const QPointF from = toScreen(m_nodes[size_t(a)].position);
        const QPointF to = toScreen(m_nodes[size_t(b)].position);
        painter.drawLine(from, to);
        if (m_settings.arrows && m_zoom > 0.6) { // a head just outside the target's dot
            const QPointF d = to - from;
            const qreal length = std::hypot(d.x(), d.y());
            if (length > 1) {
                const QPointF unit = d / length;
                const QPointF tip = to - unit * (radius(m_nodes[size_t(b)]) * std::clamp(m_zoom, 0.6, 2.0) + 1);
                const QPointF side(-unit.y(), unit.x());
                const qreal size = 6;
                painter.setBrush(color);
                painter.drawPolygon(QPolygonF{tip, tip - unit * size + side * size * 0.5, tip - unit * size - side * size * 0.5});
                painter.setBrush(Qt::NoBrush);
            }
        }
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
        if (!current && i != m_hover) { // tags, attachments and notes not created yet look different
            if (node.kind == Kind::Tag)
                fill = EditorTheme::mix(dot, QColor(0x2e, 0xa0, 0x43), 0.7);
            else if (node.kind == Kind::Attachment)
                fill = EditorTheme::mix(dot, QColor(0xd6, 0x8a, 0x00), 0.7);
            else if (node.kind == Kind::Missing)
                fill = EditorTheme::mix(base, text, 0.25);
        }
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
        const Node &node = m_nodes[size_t(m_dragged)];
        if (!m_moved && event->button() == Qt::LeftButton) {
            if (node.kind == Kind::Tag)
                emit tagActivated(node.name.mid(1));
            else if (node.kind != Kind::Missing)
                emit noteActivated(node.path);
        }
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
