#pragma once

#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QString>

namespace fhir {

// Pretty-printed JSON that remembers where every element landed, so an
// issue's path can be highlighted in it.
struct JsonView {
    QString text;
    QHash<QString, QPair<int, int>> ranges; // path -> [start, end) in text

    // The range of a path, or of its nearest ancestor that was printed.
    QPair<int, int> rangeOf(const QString &path) const;
};

// resourceType, id, and meta come first, as people expect; QJsonObject
// itself keeps keys in alphabetical order.
JsonView formatJson(const QJsonObject &object);

} // namespace fhir
