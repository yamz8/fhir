#pragma once

#include <QJsonObject>
#include <QString>

namespace fhir {

inline constexpr const char *kRedacted = "[redacted]";

// Elements that identify a person: names, identifiers, addresses, telecom,
// birth and death dates, photos, and narrative text, which repeats them.
bool isPhiKey(const QString &key);
// Whether a path ("resource.name[0].family") runs through one of them.
bool isPhiPath(const QString &path);
// A Reference whose display names a person.
bool isPersonReference(const QString &key, const QJsonObject &reference);

// A copy with every identifying value replaced by kRedacted, keeping the shape.
QJsonValue redact(const QJsonValue &value, const QString &key = {});

} // namespace fhir
