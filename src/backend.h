#pragma once

#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>

#include "fhirfile.h"
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

    Q_PROPERTY(QVariantList timelineRows READ timelineRows NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList timelineTypes READ timelineTypes NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList resourceRows READ resourceRows NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList issueRows READ issueRows NOTIFY issuesChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

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

    QVariantList timelineRows() const { return m_timelineRows; }
    QVariantList timelineTypes() const { return m_timelineTypes; }
    QVariantList resourceRows() const { return m_resourceRows; }
    QVariantList issueRows() const { return m_issueRows; }
    QString message() const { return m_message; }

    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void requestOpen();
    // kind is issues-json, issues-csv, timeline-csv, timeline-pdf, or timeline-svg.
    Q_INVOKABLE void requestExport(const QString &kind);
    Q_INVOKABLE bool exportTo(const QString &kind, const QString &path);

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

private:
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
    QVariantList m_timelineRows, m_timelineTypes, m_resourceRows, m_issueRows;
};
