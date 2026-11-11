#include "painteditems.h"

#include <QHash>
#include <QPainter>

#include <cmath>

IconItem::IconItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void IconItem::setPath(const QString &path)
{
    if (path == m_path)
        return;
    m_path = path;
    emit changed();
    update();
}

void IconItem::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    emit changed();
    update();
}

void IconItem::setStroke(qreal stroke)
{
    if (qFuzzyCompare(stroke, m_stroke))
        return;
    m_stroke = stroke;
    emit changed();
    update();
}

static void arcTo(QPainterPath &path, qreal r, bool largeArc, bool sweep, const QPointF &to)
{
    const QPointF from = path.currentPosition();
    const QPointF half = (from - to) / 2;
    const qreal h = std::hypot(half.x(), half.y());
    if (h < 1e-9)
        return;
    r = qMax(r, h);
    // Endpoint to centre form (SVG implementation notes, F.6.5), for a circle.
    const qreal k = std::sqrt(qMax<qreal>(0, r * r - h * h)) / h * (largeArc != sweep ? 1 : -1);
    const QPointF centre = (from + to) / 2 + QPointF(k * half.y(), -k * half.x());
    const qreal a0 = std::atan2(from.y() - centre.y(), from.x() - centre.x());
    const qreal a1 = std::atan2(to.y() - centre.y(), to.x() - centre.x());
    qreal delta = a1 - a0;
    if (sweep && delta < 0)
        delta += 2 * M_PI;
    else if (!sweep && delta > 0)
        delta -= 2 * M_PI;
    // QPainterPath counts angles the other way round (y points up there).
    path.arcTo(QRectF(centre.x() - r, centre.y() - r, 2 * r, 2 * r), -a0 * 180 / M_PI, -delta * 180 / M_PI);
}

QPainterPath IconItem::parse(const QString &svg)
{
    QPainterPath path;
    const QByteArray data = svg.toLatin1();
    const char *p = data.constData();
    const char *end = p + data.size();
    auto number = [&](qreal &out) {
        while (p < end && (*p == ' ' || *p == ','))
            ++p;
        if (p >= end || std::isalpha(static_cast<unsigned char>(*p)))
            return false;
        char *after = nullptr;
        out = std::strtod(p, &after);
        if (after == p)
            return false;
        p = after;
        return true;
    };
    char cmd = 0;
    while (p < end) {
        while (p < end && *p == ' ')
            ++p;
        if (p >= end)
            break;
        if (std::isalpha(static_cast<unsigned char>(*p)))
            cmd = *p++;
        qreal a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;
        switch (cmd) {
        case 'M':
            if (!number(a) || !number(b))
                return path;
            path.moveTo(a, b);
            cmd = 'L'; // further pairs are lines
            break;
        case 'L':
            if (!number(a) || !number(b))
                return path;
            path.lineTo(a, b);
            break;
        case 'H':
            if (!number(a))
                return path;
            path.lineTo(a, path.currentPosition().y());
            break;
        case 'V':
            if (!number(a))
                return path;
            path.lineTo(path.currentPosition().x(), a);
            break;
        case 'C':
            if (!number(a) || !number(b) || !number(c) || !number(d) || !number(e) || !number(f))
                return path;
            path.cubicTo(a, b, c, d, e, f);
            break;
        case 'a':
            if (!number(a) || !number(b) || !number(c) || !number(d) || !number(e) || !number(f) || !number(g))
                return path;
            arcTo(path, a, d != 0, e != 0, path.currentPosition() + QPointF(f, g));
            break;
        case 'Z':
        case 'z':
            path.closeSubpath();
            cmd = 0;
            break;
        default:
            return path; // not part of the subset
        }
    }
    return path;
}

void IconItem::paint(QPainter *painter)
{
    if (m_path.isEmpty() || width() <= 0)
        return;
    static QHash<QString, QPainterPath> cache;
    auto it = cache.constFind(m_path);
    if (it == cache.constEnd())
        it = cache.insert(m_path, parse(m_path));
    painter->setRenderHint(QPainter::Antialiasing);
    painter->scale(width() / 24.0, height() / 24.0);
    // At least one device-independent pixel wide, however small the icon.
    const qreal minWidth = 24.0 / qMax<qreal>(1, width());
    painter->setPen(QPen(m_color, qMax(m_stroke, minWidth), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(*it);
}

LinesItem::LinesItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void LinesItem::setSeries(const QVariantList &series)
{
    m_series = series;
    emit changed();
    update();
}

void LinesItem::setColors(const QVariantList &colors)
{
    m_colors = colors;
    emit changed();
    update();
}

void LinesItem::setCapacity(int capacity)
{
    if (capacity == m_capacity)
        return;
    m_capacity = qMax(2, capacity);
    emit changed();
    update();
}

void LinesItem::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal step = width() / qMax(1, m_capacity - 1);
    for (int s = 0; s < m_series.size(); ++s) {
        const QVariantList values = m_series[s].toList();
        if (values.size() < 2)
            continue;
        const int offset = m_capacity - values.size();
        QPolygonF line;
        line.reserve(values.size());
        for (int i = 0; i < values.size(); ++i) {
            const qreal v = qBound<qreal>(0, values[i].toDouble(), 100);
            line << QPointF((offset + i) * step, 1 + (height() - 2) * (1 - v / 100));
        }
        const QColor color = m_colors.isEmpty() ? QColor(Qt::white) : m_colors[s % m_colors.size()].value<QColor>();
        painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(line);
    }
}
