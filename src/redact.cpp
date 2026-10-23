#include "redact.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace fhir {

bool isPhiKey(const QString &key) {
    static const QSet<QString> keys = {"name", "identifier", "address", "telecom", "birthDate",
                                       "deceasedDateTime", "photo", "text"};
    return keys.contains(key);
}

bool isPhiPath(const QString &path) {
    static const QRegularExpression separators(R"([.\[\]])");
    for (const auto &segment : path.split(separators, Qt::SkipEmptyParts))
        if (isPhiKey(segment))
            return true;
    return false;
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
    // Narrative text is "text": {"status", "div"}; CodeableConcept.text is a
    // plain string that says what the code means, and stays.
    if (key == "text" && !value.isObject())
        return value;
    if (isPhiKey(key))
        return mask(value);
    if (value.isArray()) {
        QJsonArray out;
        for (const auto &item : value.toArray())
            out << redact(item, key);
        return out;
    }
    if (!value.isObject())
        return value;
    QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
        it.value() = redact(it.value(), it.key());
    // A Reference's display, but not a Coding's, which has a code beside it.
    if (object.contains("display") && !object.contains("code") && isPersonReference(key, object))
        object.insert("display", kRedacted);
    return object;
}

} // namespace fhir
