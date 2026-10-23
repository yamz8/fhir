#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <algorithm>

#include "filepicker.h"
#include "jsonview.h"
#include "redact.h"
#include "report.h"
#include "validator.h"

using namespace fhir;

namespace {

int severityRank(Severity severity) {
    return severity == Severity::Error ? 0 : severity == Severity::Warning ? 1 : 2;
}

// Bundle, then the core types in the order a chart reads, then the rest.
int typeOrder(const QString &type) {
    static const QStringList order = {"Bundle", "Patient", "Encounter", "Condition", "Observation",
                                      "DiagnosticReport", "MedicationRequest", "Procedure", "Immunization",
                                      "AllergyIntolerance"};
    const int index = order.indexOf(type);
    return index < 0 ? order.size() : index;
}

void collectReferences(const QJsonValue &value, const QString &path, QList<QPair<QString, QString>> &out) {
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
            collectReferences(array[i], indexPath(path, i), out);
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.value("reference").isString())
            out << qMakePair(childPath(path, "reference"), object.value("reference").toString());
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.key() != "contained" && it.key() != "reference")
                collectReferences(it.value(), childPath(path, it.key()), out);
    }
}

} // namespace

Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker) {
    if (m_picker) {
        connect(m_picker, &FilePicker::openSelected, this, &Backend::openUrl);
        connect(m_picker, &FilePicker::saveSelected, this, [this](const QUrl &url) {
            const QString kind = m_pendingExport;
            m_pendingExport.clear();
            exportTo(kind, url.toLocalFile());
        });
        connect(m_picker, &FilePicker::failed, this, &Backend::showMessage);
    }
}

QString Backend::fileName() const {
    return QFileInfo(m_file.path).fileName();
}

QString Backend::kind() const {
    switch (m_file.kind) {
    case File::Kind::Single: return tr("Single resource");
    case File::Kind::Bundle: {
        const QString type = m_file.resources.first().json.value("type").toString();
        return type.isEmpty() ? tr("Bundle") : tr("Bundle (%1)").arg(type);
    }
    case File::Kind::Ndjson: return tr("NDJSON bulk export");
    case File::Kind::Empty: break;
    }
    return {};
}

int Backend::patientCount() const {
    int count = 0;
    for (const auto &resource : m_file.resources)
        count += resource.type == "Patient";
    return count;
}

bool Backend::openPath(const QString &path) {
    File file;
    if (!file.load(path)) {
        m_loadError = tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), file.error);
        emit fileChanged();
        return false;
    }
    m_loadError.clear();
    m_file = file;
    m_issues = validate(m_file);
    const IssueCounts counts = countIssues(m_issues);
    m_errors = counts.errors;
    m_warnings = counts.warnings;
    m_infos = counts.infos;
    m_timeline = buildTimeline(m_file, QLocale());
    m_trends = trends(m_timeline);
    // What points at each resource, found once here rather than on every selection.
    m_incoming.clear();
    for (int i = 0; i < m_file.resources.size(); ++i) {
        if (m_file.resources[i].container)
            continue;
        QList<QPair<QString, QString>> found;
        collectReferences(m_file.resources[i].shown, QString(), found);
        QSet<int> targets;
        for (const auto &[path, reference] : found) {
            const int target = m_file.resolve(reference);
            if (target >= 0 && target != i && !targets.contains(target)) {
                targets.insert(target);
                m_incoming[target] << qMakePair(i, path);
            }
        }
    }
    m_filter = Filter();
    m_collapsed.clear();
    emit fileChanged();
    emit filterChanged();
    rebuildTimelineRows();
    rebuildResourceRows();
    rebuildIssueRows();
    showMessage(QString());
    return true;
}

void Backend::openUrl(const QUrl &url) {
    openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void Backend::requestOpen() {
    if (m_picker)
        m_picker->openFhir();
}

QString Backend::suggestedPath(const QString &kind) const {
    const QFileInfo source(m_file.path);
    // Beside the source when that folder is writable, never over the source.
    QString folder = source.absolutePath();
    if (!QFileInfo(folder).isWritable())
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString stem = source.completeBaseName().isEmpty() ? QString("fhir") : source.completeBaseName();
    const QString suffix = kind.section('-', 1);
    const QString what = kind.section('-', 0, 0);
    return QDir(folder).filePath(stem + "-" + what + "." + suffix);
}

void Backend::requestExport(const QString &kind) {
    if (!loaded())
        return;
    m_pendingExport = kind;
    if (m_picker)
        m_picker->saveFile(QUrl::fromLocalFile(suggestedPath(kind)), kind.section('-', 1));
}

bool Backend::exportTo(const QString &kind, const QString &path) {
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(m_file.path).absoluteFilePath()) {
        showMessage(tr("Not exporting over the source file; it is only ever read."));
        return false;
    }
    QString error;
    bool ok = false;
    if (kind == "issues-json")
        ok = saveAtomically(path, QJsonDocument(issuesJson(m_file, m_issues, m_redact)).toJson(), &error);
    else if (kind == "issues-csv")
        ok = saveAtomically(path, issuesCsv(m_file, m_issues, m_redact), &error);
    else if (kind == "timeline-csv")
        ok = saveAtomically(path, timelineCsv(m_file, m_timeline, m_filter, m_redact), &error);
    else if (kind == "timeline-pdf" || kind == "timeline-svg")
        ok = exportTimelineDocument(path, kind.section('-', 1), m_file, m_timeline, m_filter, m_redact, QLocale(), &error);
    else
        error = tr("Unknown export");
    showMessage(ok ? tr("Exported %1").arg(QFileInfo(path).fileName())
                   : tr("Could not export %1: %2").arg(QFileInfo(path).fileName(), error));
    return ok;
}

void Backend::setRedact(bool redact) {
    if (redact == m_redact)
        return;
    m_redact = redact;
    emit redactChanged();
    rebuildTimelineRows();
    rebuildResourceRows();
    rebuildIssueRows();
}

void Backend::setSearchText(const QString &text) {
    if (text == m_filter.text)
        return;
    m_filter.text = text;
    emit filterChanged();
    // Search narrows whichever view is showing: events, resources, or issues.
    rebuildTimelineRows();
    rebuildResourceRows();
    rebuildIssueRows();
}

QStringList Backend::filterTypes() const {
    QStringList types(m_filter.types.begin(), m_filter.types.end());
    types.sort();
    return types;
}

void Backend::setTypeFilter(const QString &type, bool enabled) {
    // The filter lists the types to show; with every type on, it shows all.
    QSet<QString> available;
    for (const auto &event : m_timeline.events)
        available.insert(event.type);
    QSet<QString> shown = m_filter.types.isEmpty() ? available : m_filter.types;
    if (enabled)
        shown.insert(type);
    else
        shown.remove(type);
    m_filter.types = shown == available ? QSet<QString>() : shown;
    emit filterChanged();
    rebuildTimelineRows();
}

bool Backend::setDateRange(const QString &from, const QString &to) {
    const QDate start = QDate::fromString(from.trimmed(), Qt::ISODate);
    const QDate end = QDate::fromString(to.trimmed(), Qt::ISODate);
    if ((!from.trimmed().isEmpty() && !start.isValid()) || (!to.trimmed().isEmpty() && !end.isValid()))
        return false;
    m_filter.from = start;
    m_filter.to = end;
    emit filterChanged();
    rebuildTimelineRows();
    return true;
}

void Backend::clearFilters() {
    m_filter = Filter();
    emit filterChanged();
    rebuildTimelineRows();
}

void Backend::setSortBySeverity(bool sort) {
    if (sort == m_sortBySeverity)
        return;
    m_sortBySeverity = sort;
    rebuildIssueRows();
}

void Backend::toggleType(const QString &type) {
    if (!m_collapsed.remove(type))
        m_collapsed.insert(type);
    rebuildResourceRows();
}

void Backend::relocalize() {
    if (m_file.kind == File::Kind::Empty)
        return;
    m_timeline = buildTimeline(m_file, QLocale());
    m_trends = trends(m_timeline);
    emit fileChanged();
    rebuildTimelineRows();
    rebuildResourceRows();
    rebuildIssueRows();
}

void Backend::showMessage(const QString &message) {
    m_message = message;
    emit messageChanged();
}

bool Backend::matchesSearch(const QStringList &fields) const {
    const QString haystack = fields.join('\n');
    for (const auto &word : m_filter.text.split(' ', Qt::SkipEmptyParts))
        if (!haystack.contains(word, Qt::CaseInsensitive))
            return false;
    return true;
}

void Backend::rebuildTimelineRows() {
    m_timelineRows.clear();
    QHash<QString, int> typeCounts;
    for (int g = 0; g < m_timeline.groups.size(); ++g) {
        const PatientGroup &group = m_timeline.groups[g];
        QVariantList rows;
        for (const int index : group.events) {
            const Event &event = m_timeline.events[index];
            typeCounts[event.type] += 1;
            if (!m_filter.matches(m_file, event))
                continue;
            rows << QVariantMap{{"header", false},
                                {"event", index},
                                {"resource", event.resource},
                                {"type", event.type},
                                {"date", displayDate(event, QLocale())},
                                {"summary", summaryOf(event, m_redact)},
                                {"status", event.status},
                                {"hasTrend", m_trends.contains(event.trendKey)}};
        }
        if (rows.isEmpty())
            continue;
        m_timelineRows << QVariantMap{{"header", true},
                                      {"label", patientLabel(m_file, m_timeline, group, m_redact)},
                                      {"count", rows.size()},
                                      {"resource", group.patient}};
        m_timelineRows += rows;
    }
    m_timelineTypes.clear();
    QStringList types = typeCounts.keys();
    std::sort(types.begin(), types.end(), [](const QString &a, const QString &b) {
        return typeOrder(a) != typeOrder(b) ? typeOrder(a) < typeOrder(b) : a < b;
    });
    for (const auto &type : types)
        m_timelineTypes << QVariantMap{{"type", type},
                                       {"count", typeCounts.value(type)},
                                       {"enabled", m_filter.types.isEmpty() || m_filter.types.contains(type)}};
    emit rowsChanged();
}

void Backend::rebuildResourceRows() {
    m_resourceRows.clear();
    QHash<int, int> issueCount, worst;
    for (const auto &issue : m_issues) {
        issueCount[issue.resource] += 1;
        worst[issue.resource] = std::min(worst.value(issue.resource, 3), severityRank(issue.severity));
    }
    QHash<QString, QList<int>> byType;
    for (int i = 0; i < m_file.resources.size(); ++i)
        byType[m_file.resources[i].type.isEmpty() ? QString("(no resourceType)") : m_file.resources[i].type] << i;
    QStringList types = byType.keys();
    std::sort(types.begin(), types.end(), [](const QString &a, const QString &b) {
        return typeOrder(a) != typeOrder(b) ? typeOrder(a) < typeOrder(b) : a < b;
    });
    QHash<int, QString> summaries;
    for (const auto &event : m_timeline.events)
        summaries.insert(event.resource, summaryOf(event, m_redact));
    for (const auto &type : types) {
        const QList<int> indices = byType.value(type);
        const bool expanded = !m_collapsed.contains(type);
        m_resourceRows << QVariantMap{{"header", true}, {"type", type}, {"count", indices.size()},
                                      {"expanded", expanded}, {"resource", -1}};
        for (const int index : indices) {
            const Resource &resource = m_file.resources[index];
            QString summary = summaries.value(index);
            if (resource.type == "Patient") {
                summary = patientLabel(m_file, m_timeline, m_file.label(index), index, m_redact);
            } else if (resource.container) {
                summary = tr("%n entries", nullptr, m_file.entryCount());
            } else if (summary.isEmpty()) {
                summary = conceptText(resource.json.value("code"));
            }
            const QString label = m_file.label(index, m_redact);
            if (!expanded || !matchesSearch({label, summary}))
                continue;
            m_resourceRows << QVariantMap{{"header", false},
                                          {"type", type},
                                          {"resource", index},
                                          {"label", label},
                                          {"summary", summary},
                                          {"issues", issueCount.value(index)},
                                          {"worst", worst.value(index, 3)}};
        }
    }
    emit rowsChanged();
}

void Backend::rebuildIssueRows() {
    QList<int> order;
    for (int i = 0; i < m_issues.size(); ++i)
        order << i;
    if (m_sortBySeverity)
        std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
            return severityRank(m_issues[a].severity) < severityRank(m_issues[b].severity);
        });
    m_issueRows.clear();
    for (const int i : order) {
        const Issue &issue = m_issues[i];
        if (!matchesSearch({m_file.location(issue), issueText(issue, m_redact), severityName(issue.severity)}))
            continue;
        m_issueRows << QVariantMap{{"issue", i},
                                   {"severity", severityName(issue.severity)},
                                   {"rank", severityRank(issue.severity)},
                                   {"location", m_file.location(issue)},
                                   {"message", issueText(issue, m_redact)},
                                   {"resource", issue.resource},
                                   {"path", issue.path}};
    }
    emit issuesChanged();
}

QVariantMap Backend::resourceView(int index, const QString &path) const {
    if (index < 0 || index >= m_file.resources.size())
        return {};
    const Resource &resource = m_file.resources[index];
    const QJsonObject shown = m_redact ? fhir::redact(resource.shown).toObject() : resource.shown;
    const JsonView view = formatJson(shown);
    const auto range = path.isEmpty() ? qMakePair(0, 0) : view.rangeOf(path);
    QStringList notes;
    for (const auto &issue : m_issues)
        if (issue.resource == index && (path.isEmpty() || issue.path == path))
            notes << severityName(issue.severity) + ": " + issueText(issue, m_redact);
    return {{"text", view.text},
            {"start", range.first},
            {"end", range.second},
            {"label", m_file.label(index, m_redact)},
            {"type", resource.type},
            {"location", m_file.location(Issue{Severity::Info, index, QString(), QString(), QString()})},
            {"fullUrl", m_redact ? redactReference(resource.fullUrl) : resource.fullUrl},
            {"issues", notes}};
}

QVariantList Backend::references(int index) const {
    QVariantList out;
    if (index < 0 || index >= m_file.resources.size())
        return out;
    QList<QPair<QString, QString>> found;
    collectReferences(m_file.resources[index].shown, QString(), found);
    for (const auto &[path, reference] : found) {
        const int target = m_file.resolve(reference);
        out << QVariantMap{{"path", path},
                           {"reference", m_redact ? redactReference(reference) : reference},
                           {"target", target},
                           {"targetLabel", target >= 0 ? m_file.label(target, m_redact) : QString()},
                           {"incoming", false}};
    }
    // What points here, so a Patient leads to its encounters and results.
    for (const auto &[source, path] : m_incoming.value(index))
        out << QVariantMap{{"path", path},
                           {"reference", QString()},
                           {"target", source},
                           {"targetLabel", m_file.label(source, m_redact)},
                           {"incoming", true}};
    return out;
}

QVariantMap Backend::eventDetail(int index) const {
    if (index < 0 || index >= m_timeline.events.size())
        return {};
    const Event &event = m_timeline.events[index];
    QString patient;
    for (int g = 0; g < m_timeline.groups.size(); ++g)
        if (m_timeline.groups[g].events.contains(index))
            patient = patientLabel(m_file, m_timeline, m_timeline.groups[g], m_redact);
    QVariantMap detail{{"summary", summaryOf(event, m_redact)},
                       {"type", event.type},
                       {"date", displayDate(event, QLocale())},
                       {"rawDate", event.date},
                       {"status", event.status},
                       {"codes", event.codes},
                       {"codeText", event.codeText},
                       {"patient", patient},
                       {"resource", event.resource},
                       {"label", m_file.label(event.resource, m_redact)},
                       {"hasTrend", false}};
    if (m_trends.contains(event.trendKey)) {
        const TrendSeries series = trendSeries(m_timeline, m_trends.value(event.trendKey));
        QVariantList points;
        for (const auto &point : series.points)
            points << QVariantMap{{"when", point.when.toMSecsSinceEpoch()}, {"value", point.value}};
        detail.insert("hasTrend", true);
        detail.insert("trendTitle", series.title);
        detail.insert("trendUnit", series.unit);
        detail.insert("trendPoints", points);
        detail.insert("trendCount", points.size());
    }
    return detail;
}

int Backend::revealResource(int index) {
    if (index < 0 || index >= m_file.resources.size())
        return -1;
    const QString type = m_file.resources[index].type.isEmpty() ? QString("(no resourceType)")
                                                                : m_file.resources[index].type;
    if (m_collapsed.remove(type))
        rebuildResourceRows();
    for (int i = 0; i < m_resourceRows.size(); ++i) {
        const QVariantMap row = m_resourceRows[i].toMap();
        if (!row.value("header").toBool() && row.value("resource").toInt() == index)
            return i;
    }
    return -1;
}
