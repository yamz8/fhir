#include "timeline.h"

#include <QCoreApplication>
#include <QTimeZone>
#include <QJsonArray>
#include <QMap>
#include <algorithm>

namespace fhir {

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("Timeline", text);
}

QString number(double value, const QLocale &locale) {
    return locale.toString(value, 'g', QLocale::FloatingPointShortest);
}

QString quantity(const QJsonObject &q, const QLocale &locale) {
    if (!q.value("value").isDouble())
        return {};
    const QString unit = !q.value("unit").toString().isEmpty() ? q.value("unit").toString() : q.value("code").toString();
    return (q.value("comparator").toString() + number(q.value("value").toDouble(), locale) +
            (unit.isEmpty() ? QString() : " " + unit))
        .trimmed();
}

QString unitOf(const QJsonObject &q) {
    return !q.value("unit").toString().isEmpty() ? q.value("unit").toString() : q.value("code").toString();
}

QString reference(const File &file, const QJsonValue &value) {
    const QJsonObject ref = value.toObject();
    if (!ref.value("display").toString().isEmpty())
        return ref.value("display").toString();
    const int target = file.resolve(ref.value("reference").toString());
    if (target >= 0)
        return conceptText(file.resources[target].json.value("code"));
    return ref.value("reference").toString();
}

// The value of an Observation or component, as one short phrase.
QString valueText(const QJsonObject &o, const QLocale &locale) {
    if (o.contains("valueQuantity"))
        return quantity(o.value("valueQuantity").toObject(), locale);
    if (o.contains("valueCodeableConcept"))
        return conceptText(o.value("valueCodeableConcept"));
    if (o.contains("valueString"))
        return o.value("valueString").toString();
    if (o.contains("valueBoolean"))
        return o.value("valueBoolean").toBool() ? tr("yes") : tr("no");
    if (o.contains("valueInteger"))
        return locale.toString(o.value("valueInteger").toInt());
    if (o.contains("valueRange")) {
        const QJsonObject range = o.value("valueRange").toObject();
        return quantity(range.value("low").toObject(), locale) + "–" + quantity(range.value("high").toObject(), locale);
    }
    if (o.contains("valueDateTime"))
        return o.value("valueDateTime").toString();
    if (o.contains("dataAbsentReason"))
        return tr("no value: %1").arg(conceptText(o.value("dataAbsentReason")));
    return {};
}

QString observationSummary(const QJsonObject &o, const QLocale &locale) {
    const QString name = conceptText(o.value("code"));
    QString value = valueText(o, locale);
    const QJsonArray components = o.value("component").toArray();
    if (value.isEmpty() && !components.isEmpty()) {
        // Blood pressure reads 120/80 mmHg: values that share a unit are
        // joined, anything else is spelled out by name.
        QStringList numbers, parts;
        QString unit;
        bool shared = true;
        for (const auto &item : components) {
            const QJsonObject component = item.toObject();
            const QJsonObject q = component.value("valueQuantity").toObject();
            if (!q.value("value").isDouble() || (!unit.isEmpty() && unitOf(q) != unit))
                shared = false;
            if (unit.isEmpty())
                unit = unitOf(q);
            numbers << number(q.value("value").toDouble(), locale);
            parts << conceptText(component.value("code")) + " " + valueText(component, locale);
        }
        value = shared ? numbers.join('/') + (unit.isEmpty() ? QString() : " " + unit) : parts.join("; ");
    }
    return value.isEmpty() ? name : name + " " + value;
}

// SNOMED CT route codes and display names, as prescriptions abbreviate them.
QString routeText(const QJsonValue &value) {
    static const QHash<QString, QString> codes = {
        {"26643006", "PO"},  {"47625008", "IV"},  {"78421000", "IM"}, {"34206005", "SC"},
        {"6064005", "TOP"},  {"37161004", "PR"},  {"37839007", "SL"}, {"18679011000001101", "INH"},
    };
    static const QHash<QString, QString> names = {
        {"oral", "PO"}, {"oral use", "PO"}, {"by mouth", "PO"}, {"intravenous", "IV"}, {"intramuscular", "IM"},
        {"subcutaneous", "SC"}, {"topical", "TOP"}, {"rectal", "PR"}, {"sublingual", "SL"}, {"inhalation", "INH"},
    };
    for (const auto &coding : value.toObject().value("coding").toArray())
        if (codes.contains(coding.toObject().value("code").toString()))
            return codes.value(coding.toObject().value("code").toString());
    const QString text = conceptText(value);
    return names.value(text.toLower(), text);
}

QString timingText(const QJsonObject &timing) {
    const QString code = conceptText(timing.value("code"));
    if (!code.isEmpty())
        return code;
    const QJsonObject repeat = timing.value("repeat").toObject();
    const int frequency = repeat.value("frequency").toInt(1);
    const double period = repeat.value("period").toDouble(0);
    const QString unit = repeat.value("periodUnit").toString();
    if (period <= 0 || unit.isEmpty())
        return {};
    if (unit == "d" && period == 1) {
        static const QStringList perDay = {"", "daily", "BID", "TID", "QID"};
        return frequency < perDay.size() ? perDay[frequency] : QString("%1×/d").arg(frequency);
    }
    if (unit == "h" && frequency == 1)
        return QString("q%1h").arg(period);
    if (unit == "wk" && period == 1 && frequency == 1)
        return "weekly";
    return QString("%1×/%2%3").arg(frequency).arg(period == 1 ? QString() : QString::number(period), unit);
}

QString medicationSummary(const File &file, const QJsonObject &m, const QLocale &locale) {
    QString name = conceptText(m.value("medicationCodeableConcept"));
    if (name.isEmpty())
        name = reference(file, m.value("medicationReference"));
    const QJsonObject dosage = m.value("dosageInstruction").toArray().first().toObject();
    QStringList parts{name};
    const QJsonObject rate = dosage.value("doseAndRate").toArray().first().toObject();
    if (rate.contains("doseQuantity"))
        parts << quantity(rate.value("doseQuantity").toObject(), locale);
    if (dosage.contains("route"))
        parts << routeText(dosage.value("route"));
    if (dosage.contains("timing"))
        parts << timingText(dosage.value("timing").toObject());
    if (dosage.value("asNeededBoolean").toBool())
        parts << "PRN";
    parts.removeAll(QString());
    if (parts.size() == 1 && !dosage.value("text").toString().isEmpty())
        parts << dosage.value("text").toString();
    return parts.join(' ');
}

QString summarize(const File &file, const Resource &resource, const QLocale &locale) {
    const QJsonObject &j = resource.json;
    const QString type = resource.type;
    if (type == "Observation")
        return observationSummary(j, locale);
    if (type == "MedicationRequest")
        return medicationSummary(file, j, locale);
    if (type == "Encounter") {
        QString text = conceptText(j.value("type").toArray().first());
        if (text.isEmpty())
            text = j.value("class").toObject().value("display").toString();
        if (text.isEmpty())
            text = tr("Encounter (%1)").arg(j.value("class").toObject().value("code").toString());
        const QString reason = conceptText(j.value("reasonCode").toArray().first());
        return reason.isEmpty() ? text : text + " · " + reason;
    }
    if (type == "Condition") {
        const QString status = j.value("clinicalStatus").toObject().value("coding").toArray().first()
                                   .toObject().value("code").toString();
        const QString text = conceptText(j.value("code"));
        return status.isEmpty() || status == "active" ? text : text + " (" + status + ")";
    }
    if (type == "Immunization") {
        const QString text = conceptText(j.value("vaccineCode"));
        return j.value("status").toString() == "not-done" ? text + " (" + tr("not done") + ")" : text;
    }
    if (type == "DiagnosticReport") {
        const QString text = conceptText(j.value("code"));
        const QString conclusion = j.value("conclusion").toString();
        return conclusion.isEmpty() ? text : text + ": " + conclusion;
    }
    if (type == "AllergyIntolerance") {
        const QString text = conceptText(j.value("code"));
        const QString criticality = j.value("criticality").toString();
        return criticality.isEmpty() ? text : text + " (" + tr("%1 criticality").arg(criticality) + ")";
    }
    return conceptText(j.value("code"));
}

QStringList dateKeys(const QString &type) {
    if (type == "Encounter") return {"period.start"};
    if (type == "Observation" || type == "DiagnosticReport")
        return {"effectiveDateTime", "effectivePeriod.start", "effectiveInstant", "issued"};
    if (type == "Condition") return {"onsetDateTime", "onsetPeriod.start", "recordedDate"};
    if (type == "MedicationRequest") return {"authoredOn"};
    if (type == "Procedure") return {"performedDateTime", "performedPeriod.start"};
    if (type == "Immunization") return {"occurrenceDateTime"};
    if (type == "AllergyIntolerance") return {"onsetDateTime", "recordedDate"};
    return {};
}

QString valueAt(const QJsonObject &json, const QString &dotted) {
    QJsonValue value = json;
    for (const auto &part : dotted.split('.'))
        value = value.toObject().value(part);
    return value.toString();
}

} // namespace

QDateTime parseFhirDate(const QString &value, bool *hasTime) {
    if (hasTime)
        *hasTime = value.contains('T');
    if (value.contains('T')) {
        QDateTime when = QDateTime::fromString(value, Qt::ISODateWithMs);
        return when.isValid() ? when.toUTC() : QDateTime();
    }
    const QStringList parts = value.split('-');
    const int year = parts.value(0).toInt();
    const int month = parts.size() > 1 ? parts[1].toInt() : 1;
    const int day = parts.size() > 2 ? parts[2].toInt() : 1;
    const QDate date(year, month, day);
    return date.isValid() ? QDateTime(date, QTime(0, 0), QTimeZone::UTC) : QDateTime();
}

Timeline buildTimeline(const File &file, const QLocale &locale) {
    QList<Event> events;
    for (int i = 0; i < file.resources.size(); ++i) {
        const Resource &resource = file.resources[i];
        if (resource.container)
            continue;
        for (const auto &key : dateKeys(resource.type)) {
            const QString date = valueAt(resource.json, key);
            bool hasTime = false;
            const QDateTime when = parseFhirDate(date, &hasTime);
            if (!when.isValid())
                continue;
            Event event;
            event.resource = i;
            event.type = resource.type;
            event.date = date;
            event.when = when;
            event.hasTime = hasTime;
            event.summary = summarize(file, resource, locale);
            event.status = resource.json.value("status").toString();
            if (event.status.isEmpty())
                event.status = resource.json.value("clinicalStatus").toObject().value("coding").toArray()
                                   .first().toObject().value("code").toString();
            const QString codeKey = resource.type == "Immunization" ? "vaccineCode"
                                    : resource.type == "MedicationRequest" ? "medicationCodeableConcept"
                                                                           : "code";
            const QJsonValue coded = resource.type == "Encounter" ? resource.json.value("type").toArray().first()
                                                                    : resource.json.value(codeKey);
            event.codes = conceptCodes(coded);
            event.codeText = conceptText(coded);
            event.patient = file.patientOf(i);
            event.patientKey = event.patient >= 0 ? file.label(event.patient) : file.patientReference(i);
            const QJsonObject q = resource.json.value("valueQuantity").toObject();
            if (resource.type == "Observation" && q.value("value").isDouble()) {
                event.numeric = true;
                event.value = q.value("value").toDouble();
                event.unit = unitOf(q);
                const QString code = event.codes.isEmpty() ? event.codeText : event.codes.first();
                if (!code.isEmpty())
                    event.trendKey = event.patientKey + "\n" + code + "\n" + event.unit;
            }
            events << event;
            break;
        }
    }

    // Patients in the order the file lists them, then references to patients
    // that aren't in it, then events about no one.
    QMap<QString, int> order;
    for (int i = 0; i < file.resources.size(); ++i)
        if (file.resources[i].type == "Patient")
            order.insert(file.label(i), i);
    auto rank = [&](const Event &event) {
        if (event.patientKey.isEmpty())
            return std::make_pair(2, QString());
        if (order.contains(event.patientKey))
            return std::make_pair(0, QString("%1").arg(order.value(event.patientKey), 8, 10, QChar('0')));
        return std::make_pair(1, event.patientKey);
    };
    std::stable_sort(events.begin(), events.end(), [&](const Event &a, const Event &b) {
        const auto ra = rank(a), rb = rank(b);
        if (ra != rb)
            return ra < rb;
        return a.when < b.when;
    });

    Timeline timeline;
    timeline.events = events;
    for (int i = 0; i < events.size(); ++i) {
        if (timeline.groups.isEmpty() || timeline.groups.last().key != events[i].patientKey)
            timeline.groups << PatientGroup{events[i].patientKey, events[i].patient, {}};
        timeline.groups.last().events << i;
    }
    return timeline;
}

QString patientLabel(const File &file, const PatientGroup &group, int ordinal, bool redacted) {
    if (group.key.isEmpty())
        return tr("No patient");
    if (redacted)
        return tr("Patient %1").arg(ordinal);
    if (group.patient >= 0) {
        const QString name = patientName(file.resources[group.patient].json);
        return name.isEmpty() ? group.key : name;
    }
    return tr("%1 (not in file)").arg(group.key);
}

QString displayDate(const Event &event, const QLocale &locale) {
    if (event.date.size() < 10)
        return event.date;
    if (!event.hasTime)
        return locale.toString(event.when.date(), QLocale::ShortFormat);
    // As recorded, in the offset it was written with, not this machine's zone.
    const QDateTime recorded = QDateTime::fromString(event.date, Qt::ISODateWithMs);
    return locale.toString(recorded.isValid() ? recorded : event.when, QLocale::ShortFormat);
}

bool Filter::matches(const File &file, const Event &event) const {
    if (!types.isEmpty() && !types.contains(event.type))
        return false;
    if (from.isValid() || to.isValid()) {
        // The day as written, in the time zone it was recorded in.
        const QDate day = parseFhirDate(event.date.left(10)).date();
        if ((from.isValid() && day < from) || (to.isValid() && day > to))
            return false;
    }
    if (!patient.isEmpty()) {
        const QString id = event.patient >= 0 ? file.resources[event.patient].id : QString();
        if (patient != id && patient != event.patientKey && !event.patientKey.endsWith("/" + patient))
            return false;
    }
    if (!text.isEmpty()) {
        const QString haystack = QStringList{event.summary, event.type, event.status, event.codeText,
                                             event.codes.join(' ')}
                                     .join('\n');
        for (const auto &word : text.split(' ', Qt::SkipEmptyParts))
            if (!haystack.contains(word, Qt::CaseInsensitive))
                return false;
    }
    return true;
}

QHash<QString, QList<int>> trends(const Timeline &timeline) {
    QHash<QString, QList<int>> found;
    for (int i = 0; i < timeline.events.size(); ++i)
        if (!timeline.events[i].trendKey.isEmpty())
            found[timeline.events[i].trendKey] << i;
    for (auto it = found.begin(); it != found.end();)
        it = it.value().size() < 2 ? found.erase(it) : std::next(it);
    return found;
}

} // namespace fhir
