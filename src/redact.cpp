#include "redact.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace fhir {

bool isPhiKey(const QString &key) {
    // Notes, comments, conclusions, and free-text values are written by people
    // and can name anyone.
    static const QSet<QString> keys = {"name", "identifier", "address", "telecom", "birthDate",
                                       "deceasedDateTime", "photo", "text", "note", "comment",
                                       "conclusion", "valueString", "patientInstruction"};
    return keys.contains(key);
}

bool isPhiPath(const QString &path) {
    static const QRegularExpression separators(R"([.\[\]])");
    for (const auto &segment : path.split(separators, Qt::SkipEmptyParts))
        if (isPhiKey(segment))
            return true;
    return false;
}

bool isPersonType(const QString &type) {
    static const QSet<QString> types = {"Patient", "Practitioner", "PractitionerRole", "RelatedPerson", "Person"};
    return types.contains(type);
}

QString redactReference(const QString &reference) {
    static const QRegularExpression person(
        R"(((?:^|/)(?:Patient|Practitioner|PractitionerRole|RelatedPerson|Person)/)[^/]+((?:/_history/[^/]+)?)$)");
    QString out = reference;
    const auto match = person.match(reference);
    if (match.hasMatch())
        out.replace(match.capturedStart(), match.capturedLength(), match.captured(1) + kRedacted + match.captured(2));
    return out;
}

bool isPersonReference(const QString &key, const QJsonObject &reference) {
    static const QSet<QString> personKeys = {"subject", "patient", "individual", "performer", "requester",
                                             "recorder", "asserter", "author", "actor", "participant",
                                             "generalPractitioner", "informationSource"};
    static const QRegularExpression personTypes(R"((^|/)(Patient|Practitioner|PractitionerRole|RelatedPerson|Person)/)");
    const QString target = reference.value("reference").toString();
    return personKeys.contains(key) || personTypes.match(target).hasMatch();
}

namespace {

QJsonValue mask(const QJsonValue &value) {
    if (value.isArray()) {
        QJsonArray masked;
        for (const auto &item : value.toArray())
            masked << mask(item);
        return masked;
    }
    if (value.isObject()) {
        QJsonObject masked = value.toObject();
        for (auto it = masked.begin(); it != masked.end(); ++it)
            // Codes say what kind of value it was (home, phone), not whose.
            if (it.key() != "use" && it.key() != "system" && it.key() != "status" && it.key() != "type")
                it.value() = mask(it.value());
        return masked;
    }
    return QString(kRedacted);
}

} // namespace

QJsonValue redact(const QJsonValue &value, const QString &key) {
    if (key == "text" && !value.isObject())
        return mask(value);
    if (isPhiKey(key))
        return mask(value);
    if ((key == "reference" || key == "fullUrl") && value.isString())
        return redactReference(value.toString());
    if (value.isArray()) {
        QJsonArray out;
        for (const auto &item : value.toArray())
            out << redact(item, key);
        return out;
    }
    if (!value.isObject())
        return value;
    QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        // A CodeableConcept's text says what its codes mean and stays; any
        // other plain text (Dosage.text, Annotation.text) is free text.
        if (it.key() == "text" && it.value().isString() && object.contains("coding"))
            continue;
        it.value() = redact(it.value(), it.key());
    }
    if (isPersonType(object.value("resourceType").toString()) && object.contains("id"))
        object.insert("id", kRedacted);
    // A Reference's display, but not a Coding's, which has a code beside it.
    if (object.contains("display") && !object.contains("code") && isPersonReference(key, object))
        object.insert("display", kRedacted);
    return object;
}

} // namespace fhir
