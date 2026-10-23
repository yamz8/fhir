#pragma once

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QList>
#include <QQuickPaintedItem>
#include <QVariantList>

class QPainter;

namespace fhir {

struct TrendPoint {
    QDateTime when;
    double value = 0;
};

struct TrendSeries {
    QString title, unit;
    QList<TrendPoint> points; // in date order
};

struct TrendStyle {
    QColor background = Qt::transparent;
    QColor text = QColor("#e6e6e6");
    QColor muted = QColor("#8a8a8e");
    QColor grid = QColor("#2a2a30");
    QColor line = QColor("#FFD60A");
    QFont font;
};

// The one painter for trends, used on screen by TrendChart and by the PDF
// and SVG exports, so both show the same chart.
void paintTrend(QPainter *painter, const QRectF &rect, const TrendSeries &series, const TrendStyle &style,
                const QLocale &locale);

} // namespace fhir

// A trend on screen. `points` is a list of {when: msecs since epoch, value}.
class TrendChart : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY changed)
    Q_PROPERTY(QVariantList points READ points WRITE setPoints NOTIFY changed)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY changed)

public:
    explicit TrendChart(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QString title() const { return m_series.title; }
    QString unit() const { return m_series.unit; }
    QVariantList points() const { return m_points; }
    QColor lineColor() const { return m_style.line; }
    void setTitle(const QString &title);
    void setUnit(const QString &unit);
    void setPoints(const QVariantList &points);
    void setLineColor(const QColor &color);

signals:
    void changed();

private:
    fhir::TrendSeries m_series;
    fhir::TrendStyle m_style;
    QVariantList m_points;
};
