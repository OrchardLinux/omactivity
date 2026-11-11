// Icons and the per-core line graph, drawn with QPainter. QtQuick.Shapes
// would do the same in QML, but the software renderer (what Omarchy A uses
// on boards without a GPU) does not clip shapes to the list they scroll in.
#pragma once

#include <QColor>
#include <QPainterPath>
#include <QQuickPaintedItem>
#include <QVariant>

// A line icon from an SVG path on a 24 x 24 grid, stroked with round caps.
class IconItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(qreal stroke READ stroke WRITE setStroke NOTIFY changed)

public:
    explicit IconItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QString path() const { return m_path; }
    void setPath(const QString &path);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    qreal stroke() const { return m_stroke; }
    void setStroke(qreal stroke);

    // The subset of SVG path syntax the icons use: M L H V C Z, and a
    // (relative circular arcs).
    static QPainterPath parse(const QString &svg);

signals:
    void changed();

private:
    QString m_path;
    QColor m_color;
    qreal m_stroke = 2;
};

// One line per series, values 0..100, the newest at the right edge.
class LinesItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList series READ series WRITE setSeries NOTIFY changed)
    Q_PROPERTY(QVariantList colors READ colors WRITE setColors NOTIFY changed)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY changed)

public:
    explicit LinesItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QVariantList series() const { return m_series; }
    void setSeries(const QVariantList &series);
    QVariantList colors() const { return m_colors; }
    void setColors(const QVariantList &colors);
    int capacity() const { return m_capacity; }
    void setCapacity(int capacity);

signals:
    void changed();

private:
    QVariantList m_series;
    QVariantList m_colors;
    int m_capacity = 60;
};
