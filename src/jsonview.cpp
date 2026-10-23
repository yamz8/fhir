#include "jsonview.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <algorithm>

#include "fhirfile.h"

namespace fhir {

namespace {

QStringList orderedKeys(const QJsonObject &object) {
    static const QStringList first = {"fullUrl", "resource", "resourceType", "id", "meta", "text"};
    QStringList keys;
    for (const auto &key : first)
        if (object.contains(key))
            keys << key;
    for (const auto &key : object.keys())
        if (!first.contains(key))
            keys << key;
    return keys;
}

QString scalar(const QJsonValue &value) {
    // QJsonDocument writes scalars exactly as JSON would, escapes and all.
    const QByteArray wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

void write(JsonView &view, const QJsonValue &value, const QString &path, int depth) {
    const QString indent(depth * 2, ' '), inner((depth + 1) * 2, ' ');
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            view.text += "{}";
            return;
        }
        view.text += "{\n";
        const QStringList keys = orderedKeys(object);
        for (int i = 0; i < keys.size(); ++i) {
            view.text += inner;
            const int start = view.text.size();
            view.text += scalar(keys[i]) + ": ";
            const QString child = childPath(path, keys[i]);
            write(view, object.value(keys[i]), child, depth + 1);
            view.ranges.insert(child, {start, int(view.text.size())});
            view.text += i + 1 < keys.size() ? ",\n" : "\n";
        }
        view.text += indent + "}";
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            view.text += "[]";
            return;
        }
        view.text += "[\n";
        for (int i = 0; i < array.size(); ++i) {
            view.text += inner;
            const int start = view.text.size();
            const QString child = indexPath(path, i);
            write(view, array[i], child, depth + 1);
            view.ranges.insert(child, {start, int(view.text.size())});
            view.text += i + 1 < array.size() ? ",\n" : "\n";
        }
        view.text += indent + "]";
    } else {
        view.text += scalar(value);
    }
}

} // namespace

QPair<int, int> JsonView::rangeOf(const QString &path) const {
    QString current = path;
    while (!current.isEmpty()) {
        if (ranges.contains(current))
            return ranges.value(current);
        const int cut = std::max(current.lastIndexOf('.'), current.lastIndexOf('['));
        current = cut > 0 ? current.left(cut) : QString();
    }
    return {0, 0};
}

JsonView formatJson(const QJsonObject &object) {
    JsonView view;
    write(view, object, QString(), 0);
    view.text += "\n";
    return view;
}

} // namespace fhir
