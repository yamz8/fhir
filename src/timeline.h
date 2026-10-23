#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QLocale>
#include <QSet>
#include <QString>
#include <QStringList>

#include "fhirfile.h"

namespace fhir {

// One dated thing that happened to a patient.
struct Event {
    int resource = -1;
    QString type;
    QString date;       // as written in the file
    QDateTime when;     // UTC, for ordering; partial dates start at their first instant
    bool hasTime = false;
    QString summary;    // "Hemoglobin A1c 7.2 %"
    QString redactedSummary; // the same, with free text (notes, conclusions, strings) hidden
    QString status;
    QStringList codes;  // "system|code", LOINC first
    QString patientKey; // the Patient resource's label, or the raw reference
    int patient = -1;   // Patient resource index, or -1
    // Numeric observations that share a patient, code, and unit form a trend.
    QString trendKey;
    double value = 0;
    bool numeric = false;
    QString unit, codeText;
};

struct PatientGroup {
    QString key;
    int patient = -1;
    QList<int> events; // indices into the timeline, in date order
};

struct Timeline {
    QList<Event> events;       // grouped by patient, then in date order
    QList<PatientGroup> groups;
    // "Patient N" for every patient key, so redacted outputs name each person
    // the same way everywhere: Patients in file order, then references to
    // patients that aren't in the file, in the order they first appear.
    QHash<QString, int> ordinals;
};

Timeline buildTimeline(const File &file, const QLocale &locale = QLocale::c());

// "Test Patient", or "Patient 2" when redacted, or the reference when the
// Patient isn't in the file.
QString patientLabel(const File &file, const Timeline &timeline, const PatientGroup &group, bool redacted);
QString patientLabel(const File &file, const Timeline &timeline, const QString &key, int patient, bool redacted);
inline const QString &summaryOf(const Event &event, bool redacted) {
    return redacted ? event.redactedSummary : event.summary;
}
QString displayDate(const Event &event, const QLocale &locale);
QDateTime parseFhirDate(const QString &value, bool *hasTime = nullptr);

struct Filter {
    QSet<QString> types; // empty means every type
    QDate from, to;      // inclusive; null means open
    QString text;
    QString patient;     // Patient id or reference, for the command line

    bool matches(const File &file, const Event &event) const;
    bool isActive() const { return !types.isEmpty() || from.isValid() || to.isValid() || !text.isEmpty(); }
};

// Points of every trend, keyed by trendKey, only where there are two or more.
QHash<QString, QList<int>> trends(const Timeline &timeline);

} // namespace fhir
