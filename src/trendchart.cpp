#include "trendchart.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QTimeZone>
#include <algorithm>

namespace fhir {

void paintTrend(QPainter *painter, const QRectF &rect, const TrendSeries &series, const TrendStyle &style,
                const QLocale &locale) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (style.background.alpha() > 0)
        painter->fillRect(rect, style.background);

    QFont font = style.font;
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    const double line = metrics.height();
    auto number = [&](double v) { return locale.toString(v, 'g', QLocale::FloatingPointShortest); };

    // Title on the left, latest value on the right.
    painter->setPen(style.text);
    const QRectF header(rect.left(), rect.top(), rect.width(), line);
    const QString latest = series.points.isEmpty()
                               ? QString()
                               : number(series.points.last().value) + (series.unit.isEmpty() ? "" : " " + series.unit);
    const double latestWidth = metrics.horizontalAdvance(latest) + line;
    painter->drawText(header.adjusted(0, 0, -latestWidth, 0), Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(series.title, Qt::ElideRight, header.width() - latestWidth));
    QFont bold = font;
    bold.setBold(true);
    painter->setFont(bold);
    painter->drawText(header, Qt::AlignRight | Qt::AlignVCenter, latest);
    painter->setFont(font);

    if (series.points.size() < 2) {
        painter->restore();
        return;
    }

    double low = series.points.first().value, high = low;
    for (const auto &point : series.points) {
        low = std::min(low, point.value);
        high = std::max(high, point.value);
    }
    if (high - low < 1e-9) {
        low -= 1;
        high += 1;
    }
    const double pad = (high - low) * 0.12;
    low -= pad;
    high += pad;

    const double axisWidth = std::max(metrics.horizontalAdvance(number(high)), metrics.horizontalAdvance(number(low))) + 8;
    const QRectF plot(rect.left() + axisWidth, header.bottom() + line * 0.9, rect.width() - axisWidth,
                      rect.height() - line * 3.0);
    if (plot.height() < 8 || plot.width() < 16) {
        painter->restore();
        return;
    }

    // Three gridlines with their values.
    painter->setPen(QPen(style.grid, 1));
    for (int i = 0; i <= 2; ++i) {
        const double y = plot.top() + plot.height() * i / 2.0;
        painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter->setPen(style.muted);
    painter->drawText(QRectF(rect.left(), plot.top() - line / 2, axisWidth - 6, line), Qt::AlignRight | Qt::AlignVCenter,
                      number(high - pad));
    painter->drawText(QRectF(rect.left(), plot.bottom() - line / 2, axisWidth - 6, line),
                      Qt::AlignRight | Qt::AlignVCenter, number(low + pad));

    const qint64 first = series.points.first().when.toMSecsSinceEpoch();
    const qint64 last = series.points.last().when.toMSecsSinceEpoch();
    const double span = std::max<qint64>(1, last - first);
    auto at = [&](const TrendPoint &point) {
        const double x = last == first ? plot.center().x()
                                       : plot.left() + plot.width() * (point.when.toMSecsSinceEpoch() - first) / span;
        const double y = plot.bottom() - plot.height() * (point.value - low) / (high - low);
        return QPointF(x, y);
    };

    QPainterPath path;
    for (int i = 0; i < series.points.size(); ++i)
        i == 0 ? path.moveTo(at(series.points[i])) : path.lineTo(at(series.points[i]));
    painter->setPen(QPen(style.line, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    painter->setPen(Qt::NoPen);
    painter->setBrush(style.line);
    for (const auto &point : series.points)
        painter->drawEllipse(at(point), 3, 3);

    // First and last dates under the plot.
    painter->setPen(style.muted);
    const QRectF dates(plot.left(), plot.bottom() + line * 0.3, plot.width(), line);
    painter->drawText(dates, Qt::AlignLeft | Qt::AlignVCenter,
                      locale.toString(series.points.first().when.date(), QLocale::ShortFormat));
    painter->drawText(dates, Qt::AlignRight | Qt::AlignVCenter,
                      locale.toString(series.points.last().when.date(), QLocale::ShortFormat));
    painter->restore();
}

} // namespace fhir

TrendChart::TrendChart(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &TrendChart::changed, this, [this] { update(); });
}

void TrendChart::paint(QPainter *painter) {
    m_style.font.setPixelSize(12);
    fhir::paintTrend(painter, QRectF(0, 0, width(), height()), m_series, m_style, QLocale());
}

void TrendChart::setTitle(const QString &title) {
    if (title == m_series.title)
        return;
    m_series.title = title;
    emit changed();
}

void TrendChart::setUnit(const QString &unit) {
    if (unit == m_series.unit)
        return;
    m_series.unit = unit;
    emit changed();
}

void TrendChart::setPoints(const QVariantList &points) {
    m_points = points;
    m_series.points.clear();
    for (const auto &item : points) {
        const QVariantMap point = item.toMap();
        m_series.points << fhir::TrendPoint{QDateTime::fromMSecsSinceEpoch(point.value("when").toLongLong(), QTimeZone::UTC),
                                            point.value("value").toDouble()};
    }
    emit changed();
}

void TrendChart::setLineColor(const QColor &color) {
    if (color == m_style.line)
        return;
    m_style.line = color;
    emit changed();
}
