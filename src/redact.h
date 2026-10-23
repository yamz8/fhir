#pragma once

#include <QJsonObject>
#include <QString>

namespace fhir {

inline constexpr const char *kRedacted = "[redacted]";

// Elements that identify a person: names, identifiers, addresses, telecom,
// birth and death dates, photos, narrative, and free text people write
// (notes, comments, conclusions, string values), which can name anyone.
bool isPhiKey(const QString &key);
// Whether a path ("resource.name[0].family") runs through one of them.
bool isPhiPath(const QString &path);
// A Reference whose display names a person.
bool isPersonReference(const QString &key, const QJsonObject &reference);

// Patients, practitioners, and related people, whose ids are often readable
// (Patient/john-smith) or medical record numbers.
bool isPersonType(const QString &type);
// "Patient/john-smith" becomes "Patient/[redacted]", keeping any base URL and
// history; references to anything else, and urns, which say nothing, stay.
QString redactReference(const QString &reference);

// A copy with every identifying value replaced by kRedacted, keeping the shape.
QJsonValue redact(const QJsonValue &value, const QString &key = {});

} // namespace fhir
