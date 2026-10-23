#include "report.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <functional>

#include "redact.h"
#include "validator.h"

namespace fhir {

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("Report", text);
}

// Quoted when needed, and never read by a spreadsheet as a formula.
QString csvField(QString value) {
    if (!value.isEmpty() && QStringLiteral("=+-@\t\r").contains(value.front()))
        value.prepend('\'');
    if (value.contains(QRegularExpression(R"([",\n\r])")))
        value = '"' + value.replace('"', "\"\"") + '"';
    return value;
}

QByteArray csvRow(const QStringList &fields) {
    QStringList escaped;
    for (const auto &field : fields)
        escaped << csvField(field);
    return (escaped.join(',') + "\r\n").toUtf8();
}

QString fileName(const File &file) {
    return QFileInfo(file.path).fileName();
}

QString kindName(File::Kind kind) {
    switch (kind) {
    case File::Kind::Single: return "resource";
    case File::Kind::Bundle: return "bundle";
    case File::Kind::Ndjson: return "ndjson";
    case File::Kind::Empty: break;
    }
    return "empty";
}

} // namespace

QString issueValue(const Issue &issue, bool redacted) {
    if (!redacted)
        return issue.value;
    return isPhiPath(issue.path) ? QString(kRedacted) : redactReference(issue.value);
}

QString issueText(const Issue &issue, bool redacted) {
    if (issue.value.isEmpty())
        return issue.message;
    return issue.message + ": \"" + issueValue(issue, redacted) + "\"";
}

QJsonObject issuesJson(const File &file, const QList<Issue> &issues, bool redacted) {
    const IssueCounts counts = countIssues(issues);
    QJsonArray list;
    for (const auto &issue : issues) {
        QJsonObject item{{"severity", severityName(issue.severity)},
                         {"location", file.location(issue)},
                         {"path", issue.path},
                         {"message", issue.message}};
        if (!issue.value.isEmpty())
            item.insert("value", issueValue(issue, redacted));
        if (issue.resource >= 0)
            item.insert("resource", file.label(issue.resource, redacted));
        list << item;
    }
    return {{"file", fileName(file)},
            {"scope", kScope},
            {"ok", counts.errors == 0},
            {"errors", counts.errors},
            {"warnings", counts.warnings},
            {"info", counts.infos},
            {"issues", list}};
}

QByteArray issuesCsv(const File &file, const QList<Issue> &issues, bool redacted) {
    QByteArray out = csvRow({"severity", "location", "resource", "message"});
    for (const auto &issue : issues)
        out += csvRow({severityName(issue.severity), file.location(issue), file.label(issue.resource, redacted),
                       issueText(issue, redacted)});
    return out;
}

QJsonObject summaryJson(const File &file, const QList<Issue> &issues, const Timeline &timeline, bool redacted) {
    QJsonObject counts;
    for (const auto &resource : file.resources)
        if (!resource.container)
            counts.insert(resource.type.isEmpty() ? "(none)" : resource.type,
                          counts.value(resource.type.isEmpty() ? "(none)" : resource.type).toInt() + 1);
    QJsonArray patients;
    for (int i = 0; i < file.resources.size(); ++i) {
        if (file.resources[i].type != "Patient")
            continue;
        const QJsonObject json = file.resources[i].json;
        int events = 0;
        for (const auto &group : timeline.groups)
            if (group.patient == i)
                events = group.events.size();
        QJsonObject patient{{"reference", file.label(i, redacted)}, {"events", events}};
        patient.insert("name", patientLabel(file, timeline, file.label(i), i, redacted));
        if (json.contains("birthDate"))
            patient.insert("birthDate", redacted ? QString(kRedacted) : json.value("birthDate").toString());
        if (json.contains("gender"))
            patient.insert("gender", json.value("gender").toString());
        patients << patient;
    }
    // The first and last events, as written in the file.
    const Event *first = nullptr, *last = nullptr;
    for (const auto &event : timeline.events) {
        if (!first || event.when < first->when)
            first = &event;
        if (!last || event.when > last->when)
            last = &event;
    }
    const IssueCounts issueCounts = countIssues(issues);
    return {{"file", fileName(file)},
            {"kind", kindName(file.kind)},
            {"resources", file.entryCount()},
            {"counts", counts},
            {"patients", patients},
            {"events", timeline.events.size()},
            {"dateRange", QJsonObject{{"start", first ? QJsonValue(first->date) : QJsonValue()},
                                      {"end", last ? QJsonValue(last->date) : QJsonValue()}}},
            {"issues", QJsonObject{{"errors", issueCounts.errors},
                                   {"warnings", issueCounts.warnings},
                                   {"info", issueCounts.infos},
                                   {"scope", kScope}}}};
}

QJsonObject timelineJson(const File &file, const Timeline &timeline, const Filter &filter, bool redacted) {
    QJsonArray groups;
    for (int g = 0; g < timeline.groups.size(); ++g) {
        const PatientGroup &group = timeline.groups[g];
        QJsonArray events;
        for (const int index : group.events) {
            const Event &event = timeline.events[index];
            if (!filter.matches(file, event))
                continue;
            events << QJsonObject{{"date", event.date},
                                  {"type", event.type},
                                  {"summary", summaryOf(event, redacted)},
                                  {"status", event.status},
                                  {"codes", QJsonArray::fromStringList(event.codes)},
                                  {"resource", file.label(event.resource, redacted)}};
        }
        if (events.isEmpty())
            continue;
        groups << QJsonObject{{"patient", patientLabel(file, timeline, group, redacted)},
                              {"reference", group.key.isEmpty() ? QJsonValue()
                                                                : QJsonValue(redacted ? redactReference(group.key) : group.key)},
                              {"events", events}};
    }
    return {{"file", fileName(file)}, {"patients", groups}};
}

QByteArray timelineCsv(const File &file, const Timeline &timeline, const Filter &filter, bool redacted) {
    QByteArray out = csvRow({"patient", "date", "type", "summary", "status", "codes", "resource"});
    for (int g = 0; g < timeline.groups.size(); ++g) {
        const PatientGroup &group = timeline.groups[g];
        const QString label = patientLabel(file, timeline, group, redacted);
        for (const int index : group.events) {
            const Event &event = timeline.events[index];
            if (filter.matches(file, event))
                out += csvRow({label, event.date, event.type, summaryOf(event, redacted), event.status,
                               event.codes.join(' '), file.label(event.resource, redacted)});
        }
    }
    return out;
}

TrendSeries trendSeries(const Timeline &timeline, const QList<int> &events) {
    TrendSeries series;
    for (const int index : events) {
        const Event &event = timeline.events[index];
        series.title = event.codeText;
        series.unit = event.unit;
        series.points << TrendPoint{event.when, event.value};
    }
    return series;
}

bool saveAtomically(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

namespace {

// A run of the printed timeline that must stay on one page.
struct Block {
    double height;
    std::function<void(QPainter *, const QRectF &)> paint;
};

QList<Block> layoutTimeline(const File &file, const Timeline &timeline, const Filter &filter, bool redacted,
                            const QLocale &locale, double width, const QFont &base) {
    QList<Block> blocks;
    QFont title = base, heading = base, small = base;
    title.setPixelSize(base.pixelSize() * 2);
    title.setBold(true);
    heading.setPixelSize(int(base.pixelSize() * 1.35));
    heading.setBold(true);
    small.setPixelSize(int(base.pixelSize() * 0.85));
    const QColor ink("#1a1a1a"), muted("#6b6b70"), rule("#d8d8dc");
    const double line = QFontMetricsF(base).height();

    const QString name = fileName(file);
    QStringList notes{tr("Generated %1").arg(locale.toString(QDateTime::currentDateTime(), QLocale::ShortFormat)),
                      tr("Structural checks only. Not for clinical decision-making.")};
    if (redacted)
        notes << tr("Identifying details redacted.");
    if (filter.isActive())
        notes << tr("Filtered.");
    blocks << Block{QFontMetricsF(title).height() + line * 2.2, [=](QPainter *p, const QRectF &r) {
                        p->setFont(title);
                        p->setPen(ink);
                        p->drawText(QRectF(r.left(), r.top(), r.width(), QFontMetricsF(title).height()),
                                    Qt::AlignLeft | Qt::AlignVCenter, tr("Timeline: %1").arg(name));
                        p->setFont(small);
                        p->setPen(muted);
                        p->drawText(QRectF(r.left(), r.top() + QFontMetricsF(title).height() + line * 0.3, r.width(), line),
                                    Qt::AlignLeft | Qt::AlignVCenter, notes.join("  ·  "));
                    }};

    const auto found = trends(timeline);
    const double dateWidth = line * 7.5, typeWidth = line * 9;
    for (int g = 0; g < timeline.groups.size(); ++g) {
        const PatientGroup &group = timeline.groups[g];
        QList<int> visible;
        for (const int index : group.events)
            if (filter.matches(file, timeline.events[index]))
                visible << index;
        if (visible.isEmpty())
            continue;
        const QString label = patientLabel(file, timeline, group, redacted);
        const QString count = QCoreApplication::translate("Report", "%n events", nullptr, int(visible.size()));
        blocks << Block{line * 2.4, [=](QPainter *p, const QRectF &r) {
                            p->setFont(heading);
                            p->setPen(ink);
                            const QRectF text(r.left(), r.top() + line * 0.6, r.width(), line * 1.4);
                            p->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, label);
                            p->setFont(small);
                            p->setPen(muted);
                            p->drawText(text, Qt::AlignRight | Qt::AlignVCenter, count);
                            p->setPen(QPen(rule, 1));
                            p->drawLine(QPointF(r.left(), r.bottom() - 1), QPointF(r.right(), r.bottom() - 1));
                        }};

        // This patient's trends, two to a row.
        QList<TrendSeries> charts;
        QSet<QString> drawn;
        for (const int index : visible) {
            const QString key = timeline.events[index].trendKey;
            if (key.isEmpty() || drawn.contains(key) || !found.contains(key))
                continue;
            drawn.insert(key);
            QList<int> points;
            for (const int point : found.value(key))
                if (filter.matches(file, timeline.events[point]))
                    points << point;
            if (points.size() >= 2)
                charts << trendSeries(timeline, points);
        }
        for (int i = 0; i < charts.size(); i += 2) {
            const QList<TrendSeries> row = charts.mid(i, 2);
            blocks << Block{line * 8, [=](QPainter *p, const QRectF &r) {
                                TrendStyle style;
                                style.text = ink;
                                style.muted = muted;
                                style.grid = rule;
                                style.line = QColor("#2f6fd6");
                                style.font = small;
                                const double gap = line;
                                const double w = (r.width() - gap) / 2;
                                for (int c = 0; c < row.size(); ++c)
                                    paintTrend(p, QRectF(r.left() + c * (w + gap), r.top() + line * 0.6, w,
                                                         r.height() - line * 0.8),
                                               row[c], style, locale);
                            }};
        }

        for (const int index : visible) {
            const Event event = timeline.events[index];
            const QString date = displayDate(event, locale);
            const QString summary = summaryOf(event, redacted);
            const QFontMetricsF metrics(base);
            const QRectF measure(0, 0, width - dateWidth - typeWidth, 10000);
            const double height =
                std::max(line, metrics.boundingRect(measure, Qt::TextWordWrap, summary).height()) + line * 0.5;
            blocks << Block{height, [=](QPainter *p, const QRectF &r) {
                                p->setFont(base);
                                p->setPen(muted);
                                p->drawText(QRectF(r.left(), r.top(), dateWidth, line), Qt::AlignLeft | Qt::AlignVCenter,
                                            date);
                                p->drawText(QRectF(r.left() + dateWidth, r.top(), typeWidth, line),
                                            Qt::AlignLeft | Qt::AlignVCenter, event.type);
                                p->setPen(ink);
                                p->drawText(QRectF(r.left() + dateWidth + typeWidth, r.top(),
                                                   r.width() - dateWidth - typeWidth, r.height()),
                                            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, summary);
                            }};
        }
    }
    return blocks;
}

} // namespace

bool exportTimelineDocument(const QString &path, const QString &format, const File &file, const Timeline &timeline,
                            const Filter &filter, bool redacted, const QLocale &locale, QString *error) {
    const bool svg = format == "svg";
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = out.errorString();
        return false;
    }
    QFont base("sans-serif");
    base.setPixelSize(10);

    QPainter painter;
    if (svg) {
        const double width = 760, margin = 32;
        const QList<Block> blocks =
            layoutTimeline(file, timeline, filter, redacted, locale, width - 2 * margin, base);
        double height = 2 * margin;
        for (const auto &block : blocks)
            height += block.height;
        QSvgGenerator generator;
        generator.setOutputDevice(&out);
        generator.setSize(QSize(int(width), int(height)));
        generator.setViewBox(QRectF(0, 0, width, height));
        generator.setTitle(tr("Timeline: %1").arg(fileName(file)));
        if (!painter.begin(&generator)) {
            if (error)
                *error = tr("Could not draw the SVG");
            return false;
        }
        painter.fillRect(QRectF(0, 0, width, height), Qt::white);
        double y = margin;
        for (const auto &block : blocks) {
            block.paint(&painter, QRectF(margin, y, width - 2 * margin, block.height));
            y += block.height;
        }
        painter.end();
    } else {
        QPdfWriter writer(&out);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(36, 36, 36, 36), QPageLayout::Point);
        writer.setTitle(tr("Timeline: %1").arg(fileName(file)));
        writer.setCreator("fhir");
        const QRectF page = writer.pageLayout().paintRectPixels(72);
        const QList<Block> blocks = layoutTimeline(file, timeline, filter, redacted, locale, page.width(), base);
        if (!painter.begin(&writer)) {
            if (error)
                *error = tr("Could not draw the PDF");
            return false;
        }
        double y = 0;
        for (const auto &block : blocks) {
            if (y > 0 && y + block.height > page.height()) {
                writer.newPage();
                y = 0;
            }
            block.paint(&painter, QRectF(0, y, page.width(), block.height));
            y += block.height;
        }
        painter.end();
    }
    if (!out.commit()) {
        if (error)
            *error = out.errorString();
        return false;
    }
    return true;
}

} // namespace fhir
