#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtConcurrentRun>
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
    m_timelineModel = new RowModel({"header", "event", "resource", "type", "date", "summary", "status", "hasTrend",
                                    "label", "count"},
                                   [this](int row, const QString &role) { return timelineCell(row, role); }, this);
    m_resourceModel = new RowModel({"header", "type", "resource", "key", "count", "expanded", "label", "summary",
                                    "issues", "worst"},
                                   [this](int row, const QString &role) { return resourceCell(row, role); }, this);
    m_issueModel = new RowModel({"issue", "severity", "rank", "location", "message", "resource", "path"},
                                [this](int row, const QString &role) { return issueCell(row, role); }, this);
    if (m_picker) {
        connect(m_picker, &FilePicker::openSelected, this, &Backend::openUrl);
        connect(m_picker, &FilePicker::saveSelected, this, [this](const QUrl &url) {
            const QString kind = m_pendingExport;
            m_pendingExport.clear();
            exportInBackground(kind, url.toLocalFile());
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

// Everything worked out from a file, built off the UI thread for large files.
struct Backend::Opened {
    File file;
    QList<Issue> issues;
    Timeline timeline;
    QHash<QString, QList<int>> trends;
    QHash<int, QList<QPair<int, QString>>> incoming;
    bool ok = false;
};

Backend::Opened Backend::openFile(const QString &path, const QLocale &locale) {
    Opened opened;
    if (!opened.file.load(path))
        return opened;
    opened.ok = true;
    opened.issues = validate(opened.file);
    opened.timeline = buildTimeline(opened.file, locale);
    opened.trends = trends(opened.timeline);
    // What points at each resource, found once here rather than on every selection.
    const File &file = opened.file;
    for (int i = 0; i < file.resources.size(); ++i) {
        if (file.resources[i].container)
            continue;
        QList<QPair<QString, QString>> found;
        collectReferences(file.resources[i].shown, QString(), found);
        QSet<int> targets;
        for (const auto &[path, reference] : found) {
            const int target = file.resolve(reference);
            if (target >= 0 && target != i && !targets.contains(target)) {
                targets.insert(target);
                opened.incoming[target] << qMakePair(i, path);
            }
        }
    }
    return opened;
}

bool Backend::apply(Opened &&opened, const QString &path) {
    if (!opened.ok) {
        m_loadError = tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), opened.file.error);
        emit fileChanged();
        return false;
    }
    m_loadError.clear();
    m_file = std::move(opened.file);
    m_issues = std::move(opened.issues);
    const IssueCounts counts = countIssues(m_issues);
    m_errors = counts.errors;
    m_warnings = counts.warnings;
    m_infos = counts.infos;
    m_timeline = std::move(opened.timeline);
    m_trends = std::move(opened.trends);
    m_incoming = std::move(opened.incoming);
    m_filter = Filter();
    m_collapsed.clear();
    indexResources();
    emit fileChanged();
    emit filterChanged();
    rebuildTimelineRows();
    rebuildResourceRows();
    rebuildIssueRows();
    showMessage(QString());
    return true;
}

bool Backend::openPath(const QString &path) {
    ++m_generation;
    return apply(openFile(path, QLocale()), path);
}

void Backend::openInBackground(const QString &path) {
    // A newer open replaces one still running; its result is dropped.
    const int generation = ++m_generation;
    m_loadingName = QFileInfo(path).fileName();
    m_loading = true;
    emit loadingChanged();
    auto *watcher = new QFutureWatcher<Opened>(this);
    connect(watcher, &QFutureWatcher<Opened>::finished, this, [this, watcher, path, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        // The data lands before loading ends, so whoever waits on loading sees it.
        apply(watcher->future().takeResult(), path);
        m_loading = false;
        emit loadingChanged();
    });
    watcher->setFuture(QtConcurrent::run(&Backend::openFile, path, QLocale()));
}

void Backend::openUrl(const QUrl &url) {
    openInBackground(url.isLocalFile() ? url.toLocalFile() : url.toString());
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

// What an export needs, copied so it can be written while the window goes on.
// Qt shares the data underneath, so the copy is cheap.
struct Backend::ExportJob {
    QString kind, path;
    File file;
    QList<Issue> issues;
    Timeline timeline;
    Filter filter;
    bool redact = false;
    QLocale locale;
};

QString Backend::runExport(const ExportJob &job) {
    QString error;
    bool ok = false;
    if (job.kind == "issues-json")
        ok = saveAtomically(job.path, QJsonDocument(issuesJson(job.file, job.issues, job.redact)).toJson(), &error);
    else if (job.kind == "issues-csv")
        ok = saveAtomically(job.path, issuesCsv(job.file, job.issues, job.redact), &error);
    else if (job.kind == "timeline-csv")
        ok = saveAtomically(job.path, timelineCsv(job.file, job.timeline, job.filter, job.redact), &error);
    else if (job.kind == "timeline-pdf" || job.kind == "timeline-svg")
        ok = exportTimelineDocument(job.path, job.kind.section('-', 1), job.file, job.timeline, job.filter, job.redact,
                                    job.locale, &error);
    else
        error = tr("Unknown export");
    return ok ? QString() : (error.isEmpty() ? tr("Unknown error") : error);
}

bool Backend::prepareExport(const QString &kind, const QString &path, ExportJob *job) {
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(m_file.path).absoluteFilePath()) {
        showMessage(tr("Not exporting over the source file; it is only ever read."));
        return false;
    }
    *job = ExportJob{kind, path, m_file, m_issues, m_timeline, m_filter, m_redact, QLocale()};
    return true;
}

void Backend::finishExport(const QString &path, const QString &error) {
    showMessage(error.isEmpty() ? tr("Exported %1").arg(QFileInfo(path).fileName())
                                : tr("Could not export %1: %2").arg(QFileInfo(path).fileName(), error));
}

bool Backend::exportTo(const QString &kind, const QString &path) {
    ExportJob job;
    if (!prepareExport(kind, path, &job))
        return false;
    const QString error = runExport(job);
    finishExport(path, error);
    return error.isEmpty();
}

void Backend::exportInBackground(const QString &kind, const QString &path) {
    ExportJob job;
    if (!prepareExport(kind, path, &job))
        return;
    ++m_exporting;
    emit exportingChanged();
    showMessage(tr("Exporting %1…").arg(QFileInfo(path).fileName()));
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, path] {
        watcher->deleteLater();
        --m_exporting;
        emit exportingChanged();
        finishExport(path, watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(&Backend::runExport, job));
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
    indexResources();
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

void Backend::indexResources() {
    m_eventOfResource.clear();
    for (int e = m_timeline.events.size() - 1; e >= 0; --e)
        m_eventOfResource.insert(m_timeline.events[e].resource, e);
    m_issueCount.clear();
    m_worst.clear();
    for (const auto &issue : m_issues) {
        m_issueCount[issue.resource] += 1;
        m_worst[issue.resource] = std::min(m_worst.value(issue.resource, 3), severityRank(issue.severity));
    }
}

QString Backend::resourceSummary(int index) const {
    const Resource &resource = m_file.resources[index];
    if (resource.type == "Patient")
        return patientLabel(m_file, m_timeline, m_file.label(index), index, m_redact);
    if (resource.container)
        return tr("%n entries", nullptr, m_file.entryCount());
    const int event = m_eventOfResource.value(index, -1);
    return event >= 0 ? summaryOf(m_timeline.events[event], m_redact) : conceptText(resource.json.value("code"));
}

QVariant Backend::timelineCell(int row, const QString &role) const {
    const TimelineRow &r = m_timelineRowList[row];
    const PatientGroup &group = m_timeline.groups[r.group];
    if (role == "header")
        return r.event < 0;
    if (r.event < 0) {
        if (role == "label") return patientLabel(m_file, m_timeline, group, m_redact);
        if (role == "count") return r.count;
        if (role == "resource") return group.patient;
        if (role == "event") return -1;
        return {};
    }
    const Event &event = m_timeline.events[r.event];
    if (role == "event") return r.event;
    if (role == "resource") return event.resource;
    if (role == "type") return event.type;
    if (role == "date") return displayDate(event, QLocale());
    if (role == "summary") return summaryOf(event, m_redact);
    if (role == "status") return event.status;
    if (role == "hasTrend") return m_trends.contains(event.trendKey);
    return {};
}

QVariant Backend::resourceCell(int row, const QString &role) const {
    const ResourceRow &r = m_resourceRowList[row];
    if (role == "header") return r.resource < 0;
    if (role == "type") return r.type;
    if (role == "resource") return r.resource;
    if (role == "key") return r.resource < 0 ? "type:" + r.type : "resource:" + QString::number(r.resource);
    if (r.resource < 0) {
        if (role == "count") return r.count;
        if (role == "expanded") return !m_collapsed.contains(r.type);
        return {};
    }
    if (role == "label") return m_file.label(r.resource, m_redact);
    if (role == "summary") return resourceSummary(r.resource);
    if (role == "issues") return m_issueCount.value(r.resource);
    if (role == "worst") return m_worst.value(r.resource, 3);
    return {};
}

QVariant Backend::issueCell(int row, const QString &role) const {
    const int i = m_issueRowList[row];
    const Issue &issue = m_issues[i];
    if (role == "issue") return i;
    if (role == "severity") return severityName(issue.severity);
    if (role == "rank") return severityRank(issue.severity);
    if (role == "location") return m_file.location(issue);
    if (role == "message") return issueText(issue, m_redact);
    if (role == "resource") return issue.resource;
    if (role == "path") return issue.path;
    return {};
}

void Backend::rebuildTimelineRows() {
    m_timelineRowList.clear();
    QHash<QString, int> typeCounts;
    for (int g = 0; g < m_timeline.groups.size(); ++g) {
        const PatientGroup &group = m_timeline.groups[g];
        const int header = m_timelineRowList.size();
        m_timelineRowList << TimelineRow{g, -1, 0};
        for (const int index : group.events) {
            const Event &event = m_timeline.events[index];
            typeCounts[event.type] += 1;
            if (m_filter.matches(m_file, event))
                m_timelineRowList << TimelineRow{g, index, 0};
        }
        m_timelineRowList[header].count = m_timelineRowList.size() - header - 1;
        if (m_timelineRowList[header].count == 0)
            m_timelineRowList.removeLast();
    }
    m_timelineModel->resetRows(m_timelineRowList.size());
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
    m_resourceRowList.clear();
    QHash<QString, QList<int>> byType;
    for (int i = 0; i < m_file.resources.size(); ++i)
        byType[m_file.resources[i].type.isEmpty() ? QString("(no resourceType)") : m_file.resources[i].type] << i;
    QStringList types = byType.keys();
    std::sort(types.begin(), types.end(), [](const QString &a, const QString &b) {
        return typeOrder(a) != typeOrder(b) ? typeOrder(a) < typeOrder(b) : a < b;
    });
    const bool searching = !m_filter.text.trimmed().isEmpty();
    for (const auto &type : types) {
        const QList<int> indices = byType.value(type);
        m_resourceRowList << ResourceRow{type, -1, int(indices.size())};
        if (m_collapsed.contains(type))
            continue;
        for (const int index : indices)
            if (!searching || matchesSearch({m_file.label(index, m_redact), resourceSummary(index)}))
                m_resourceRowList << ResourceRow{type, index, 0};
    }
    m_resourceModel->resetRows(m_resourceRowList.size());
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
    m_issueRowList.clear();
    const bool searching = !m_filter.text.trimmed().isEmpty();
    for (const int i : order) {
        const Issue &issue = m_issues[i];
        if (!searching ||
            matchesSearch({m_file.location(issue), issueText(issue, m_redact), severityName(issue.severity)}))
            m_issueRowList << i;
    }
    m_issueModel->resetRows(m_issueRowList.size());
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
    for (int i = 0; i < m_resourceRowList.size(); ++i)
        if (m_resourceRowList[i].resource == index)
            return i;
    return -1;
}
