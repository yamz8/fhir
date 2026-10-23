#pragma once

#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>

#include "fhirfile.h"
#include "rowmodel.h"
#include "timeline.h"

class FilePicker;

// The open file, its validation report, and its timeline, for QML.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY fileChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY fileChanged)
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY fileChanged)
    Q_PROPERTY(int patientCount READ patientCount NOTIFY fileChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY fileChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY fileChanged)
    Q_PROPERTY(int infoCount READ infoCount NOTIFY fileChanged)

    Q_PROPERTY(bool redact READ redact WRITE setRedact NOTIFY redactChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    Q_PROPERTY(QString dateFrom READ dateFrom NOTIFY filterChanged)
    Q_PROPERTY(QString dateTo READ dateTo NOTIFY filterChanged)
    Q_PROPERTY(QStringList filterTypes READ filterTypes NOTIFY filterChanged)
    Q_PROPERTY(bool filtering READ filtering NOTIFY filterChanged)
    Q_PROPERTY(bool sortBySeverity READ sortBySeverity WRITE setSortBySeverity NOTIFY issuesChanged)

    Q_PROPERTY(RowModel *timelineModel READ timelineModel CONSTANT)
    Q_PROPERTY(RowModel *resourceModel READ resourceModel CONSTANT)
    Q_PROPERTY(RowModel *issueModel READ issueModel CONSTANT)
    Q_PROPERTY(QVariantList timelineTypes READ timelineTypes NOTIFY rowsChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(QString loadingName READ loadingName NOTIFY loadingChanged)

public:
    explicit Backend(FilePicker *picker = nullptr, QObject *parent = nullptr);

    bool loaded() const { return m_file.kind != fhir::File::Kind::Empty || !m_file.parseIssues.isEmpty(); }
    QString fileName() const;
    QString kind() const;
    QString loadError() const { return m_loadError; }
    int resourceCount() const { return m_file.entryCount(); }
    int patientCount() const;
    int errorCount() const { return m_errors; }
    int warningCount() const { return m_warnings; }
    int infoCount() const { return m_infos; }

    bool redact() const { return m_redact; }
    void setRedact(bool redact);
    QString searchText() const { return m_filter.text; }
    void setSearchText(const QString &text);
    QString dateFrom() const { return m_filter.from.toString(Qt::ISODate); }
    QString dateTo() const { return m_filter.to.toString(Qt::ISODate); }
    QStringList filterTypes() const;
    bool filtering() const { return m_filter.isActive(); }
    bool sortBySeverity() const { return m_sortBySeverity; }
    void setSortBySeverity(bool sort);

    RowModel *timelineModel() const { return m_timelineModel; }
    RowModel *resourceModel() const { return m_resourceModel; }
    RowModel *issueModel() const { return m_issueModel; }
    // Every row at once, for tests and scripts; QML reads the models.
    QVariantList timelineRows() const { return m_timelineModel->all(); }
    QVariantList resourceRows() const { return m_resourceModel->all(); }
    QVariantList issueRows() const { return m_issueModel->all(); }
    QVariantList timelineTypes() const { return m_timelineTypes; }
    QString message() const { return m_message; }

    // Opens and checks a file before returning, as tests and scripts want.
    Q_INVOKABLE bool openPath(const QString &path);
    // Opens a file on a worker thread, so a large one doesn't freeze the window.
    Q_INVOKABLE void openInBackground(const QString &path);
    bool loading() const { return m_loading; }
    QString loadingName() const { return m_loadingName; }
    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void requestOpen();
    // kind is issues-json, issues-csv, timeline-csv, timeline-pdf, or timeline-svg.
    Q_INVOKABLE void requestExport(const QString &kind);
    // Writes the export before returning, as tests and scripts want.
    Q_INVOKABLE bool exportTo(const QString &kind, const QString &path);
    // Writes it on a worker thread, so a long PDF doesn't freeze the window.
    Q_INVOKABLE void exportInBackground(const QString &kind, const QString &path);
    bool exporting() const { return m_exporting > 0; }

    Q_INVOKABLE void setTypeFilter(const QString &type, bool enabled);
    // Dates as YYYY-MM-DD, or empty for no bound; false when one doesn't parse.
    Q_INVOKABLE bool setDateRange(const QString &from, const QString &to);
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void toggleType(const QString &type);

    Q_INVOKABLE QVariantMap resourceView(int index, const QString &path = {}) const;
    Q_INVOKABLE QVariantList references(int index) const;
    Q_INVOKABLE QVariantMap eventDetail(int event) const;
    // The row of a resource in the tree, opening its type if it was folded.
    Q_INVOKABLE int revealResource(int index);
    Q_INVOKABLE void showMessage(const QString &message);

    // Summaries and dates follow the language; the Translator calls this.
    void relocalize();

    const fhir::File &file() const { return m_file; }
    const fhir::Timeline &timeline() const { return m_timeline; }

signals:
    void fileChanged();
    void redactChanged();
    void filterChanged();
    void rowsChanged();
    void issuesChanged();
    void messageChanged();
    void loadingChanged();
    void exportingChanged();

private:
    struct Opened;
    struct ExportJob;
    static QString runExport(const ExportJob &job);
    bool prepareExport(const QString &kind, const QString &path, ExportJob *job);
    void finishExport(const QString &path, const QString &error);
    static Opened openFile(const QString &path, const QLocale &locale);
    bool apply(Opened &&opened, const QString &path);
    struct TimelineRow {
        int group, event, count; // event -1 is the patient's heading
    };
    struct ResourceRow {
        QString type;
        int resource, count; // resource -1 is the type's heading
    };
    void indexResources();
    QString resourceSummary(int index) const;
    QVariant timelineCell(int row, const QString &role) const;
    QVariant resourceCell(int row, const QString &role) const;
    QVariant issueCell(int row, const QString &role) const;
    bool matchesSearch(const QStringList &fields) const;
    void rebuildTimelineRows();
    void rebuildResourceRows();
    void rebuildIssueRows();
    QString suggestedPath(const QString &kind) const;

    FilePicker *m_picker = nullptr;
    fhir::File m_file;
    QList<fhir::Issue> m_issues;
    fhir::Timeline m_timeline;
    QHash<QString, QList<int>> m_trends;
    QHash<int, QList<QPair<int, QString>>> m_incoming; // target -> (source, path of the reference)
    fhir::Filter m_filter;
    bool m_redact = false;
    bool m_sortBySeverity = true;
    QSet<QString> m_collapsed;
    QString m_loadError, m_message, m_pendingExport;
    int m_errors = 0, m_warnings = 0, m_infos = 0;
    int m_generation = 0;
    int m_exporting = 0;
    bool m_loading = false;
    QString m_loadingName;
    QVariantList m_timelineTypes;
    QList<TimelineRow> m_timelineRowList;
    QList<ResourceRow> m_resourceRowList;
    QList<int> m_issueRowList;
    RowModel *m_timelineModel = nullptr, *m_resourceModel = nullptr, *m_issueModel = nullptr;
    QHash<int, int> m_eventOfResource, m_issueCount, m_worst;
};
