#include "validator.h"

#include <QCoreApplication>
#include <QDate>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace fhir {

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("Validator", text);
}

// The regular expressions FHIR R4 gives for its primitives.
const QString kYear = "([0-9]([0-9]([0-9][1-9]|[1-9]0)|[1-9]00)|[1-9]000)";
const QString kMonth = "(0[1-9]|1[0-2])";
const QString kDay = "(0[1-9]|[1-2][0-9]|3[0-1])";
const QString kTime = "([01][0-9]|2[0-3]):[0-5][0-9]:([0-5][0-9]|60)(\\.[0-9]+)?";
const QString kZone = "(Z|(\\+|-)((0[0-9]|1[0-3]):[0-5][0-9]|14:00))";

// The regex allows Feb 30; the calendar doesn't.
bool calendarDay(const QString &value) {
    if (value.size() < 10)
        return true;
    return QDate::fromString(value.left(10), "yyyy-MM-dd").isValid();
}

} // namespace

bool isValidDate(const QString &value) {
    static const QRegularExpression re("^" + kYear + "(-" + kMonth + "(-" + kDay + ")?)?$");
    return re.match(value).hasMatch() && calendarDay(value);
}

bool isValidDateTime(const QString &value) {
    static const QRegularExpression re("^" + kYear + "(-" + kMonth + "(-" + kDay + "(T" + kTime + kZone +
                                       ")?)?)?$");
    return re.match(value).hasMatch() && calendarDay(value);
}

bool isValidInstant(const QString &value) {
    static const QRegularExpression re("^" + kYear + "-" + kMonth + "-" + kDay + "T" + kTime + kZone + "$");
    return re.match(value).hasMatch() && calendarDay(value);
}

bool isValidId(const QString &value) {
    static const QRegularExpression re(R"(^[A-Za-z0-9\-\.]{1,64}$)");
    return re.match(value).hasMatch();
}

bool isValidCode(const QString &value) {
    static const QRegularExpression re(R"(^[^\s]+(\s[^\s]+)*$)");
    return re.match(value).hasMatch();
}

IssueCounts countIssues(const QList<Issue> &issues) {
    IssueCounts counts;
    for (const auto &issue : issues) {
        if (issue.severity == Severity::Error)
            ++counts.errors;
        else if (issue.severity == Severity::Warning)
            ++counts.warnings;
        else
            ++counts.infos;
    }
    return counts;
}

namespace {

enum class Format { None, Date, DateTime, Instant };

// The format of a primitive, from its name, as R4 types them. Names that
// mean different things in different resources are left alone.
Format formatOf(const QString &key, const QString &parentKey) {
    static const QSet<QString> dates = {"birthDate"};
    static const QSet<QString> instants = {"issued", "lastUpdated", "timestamp"};
    static const QSet<QString> dateTimes = {"recordedDate", "authoredOn", "lastOccurrence", "date",
                                            "dateAsserted", "dateWritten"};
    if (dates.contains(key))
        return Format::Date;
    // Immunization.expirationDate is a date; Medication.batch.expirationDate a dateTime.
    if (key == "expirationDate")
        return parentKey == "batch" ? Format::DateTime : Format::Date;
    if (instants.contains(key) || key.endsWith("Instant"))
        return Format::Instant;
    if (dateTimes.contains(key) || key.endsWith("DateTime"))
        return Format::DateTime;
    if ((key == "start" || key == "end") && (parentKey == "period" || parentKey.endsWith("Period")))
        return Format::DateTime;
    return Format::None;
}

class Validator {
public:
    explicit Validator(const File &file) : m_file(file) {}

    QList<Issue> run() {
        m_issues = m_file.parseIssues;
        if (m_file.kind == File::Kind::Bundle)
            checkBundle();
        QHash<QString, int> seen;
        for (int i = 0; i < m_file.resources.size(); ++i) {
            const Resource &resource = m_file.resources[i];
            if (resource.container)
                continue;
            if (resource.entry >= 0 && resource.resourcePath.isEmpty())
                continue; // An entry without a resource, reported by checkBundle.
            m_index = i;
            m_base = resource.resourcePath;
            m_contained.clear();
            for (const auto &item : resource.json.value("contained").toArray())
                m_contained.insert(item.toObject().value("id").toString());
            checkResource(resource.json, m_base, true);
            const QString key = resource.type + "/" + resource.id;
            if (!resource.id.isEmpty() && !resource.type.isEmpty()) {
                if (seen.contains(key))
                    add(Severity::Warning, childPath(m_base, "id"),
                        tr("%1 appears more than once in this file").arg(key));
                seen.insert(key, i);
            }
        }
        return m_issues;
    }

private:
    void add(Severity severity, const QString &path, const QString &message, const QString &value = {}) {
        m_issues << Issue{severity, m_index, path, message, value};
    }

    void checkBundle() {
        m_index = 0;
        m_base.clear();
        const QJsonObject bundle = m_file.resources.first().json;
        checkElement(bundle, QString(), QString(), {"entry"});
        static const QStringList bundleTypes = {"document", "message", "transaction", "transaction-response",
                                                "batch", "batch-response", "history", "searchset",
                                                "collection"};
        const QString type = bundle.value("type").toString();
        if (!bundle.contains("type"))
            add(Severity::Error, "type", tr("Bundle.type is required"));
        else if (!bundleTypes.contains(type))
            add(Severity::Error, "type", tr("Bundle.type is not a valid bundle type"), type);
        if (bundle.contains("entry") && !bundle.value("entry").isArray())
            add(Severity::Error, "entry", tr("Bundle.entry must be an array"));
        const bool request = type == "transaction" || type == "batch";

        QHash<QString, int> fullUrls;
        for (int i = 1; i < m_file.resources.size(); ++i) {
            const Resource &resource = m_file.resources[i];
            m_index = i;
            const QJsonObject entry = resource.shown;
            if (entry.isEmpty()) {
                add(Severity::Error, QString(), tr("Bundle entry is not a JSON object, or is empty"));
                continue;
            }
            for (auto it = entry.begin(); it != entry.end(); ++it)
                if (it.key() != "resource")
                    checkValue(it.value(), it.key(), it.key(), QString());
            if (resource.resourcePath.isEmpty()) {
                const bool response = type.endsWith("-response") || type == "history";
                add(response ? Severity::Info : Severity::Error, "resource",
                    response ? tr("Entry has no resource") : tr("Entry has no resource object"));
            }
            const QString fullUrl = resource.fullUrl;
            if (fullUrl.isEmpty()) {
                if (!resource.resourcePath.isEmpty() && !request)
                    add(Severity::Warning, "fullUrl", tr("Entry has no fullUrl, so other entries can't reference it"));
            } else {
                if (fullUrls.contains(fullUrl))
                    add(Severity::Error, "fullUrl",
                        tr("fullUrl is also used by entry %1").arg(m_file.resources[fullUrls.value(fullUrl)].entry),
                        fullUrl);
                else
                    fullUrls.insert(fullUrl, i);
                static const QRegularExpression uuid(
                    "^urn:uuid:[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
                static const QRegularExpression oid(R"(^urn:oid:[0-2](\.(0|[1-9][0-9]*))+$)");
                static const QRegularExpression restful(R"(/([A-Z][A-Za-z]+)/([A-Za-z0-9\-\.]{1,64})$)");
                if (fullUrl.startsWith("urn:uuid:")) {
                    if (!uuid.match(fullUrl).hasMatch())
                        add(Severity::Error, "fullUrl", tr("fullUrl is not a valid lowercase urn:uuid"), fullUrl);
                } else if (fullUrl.startsWith("urn:oid:")) {
                    if (!oid.match(fullUrl).hasMatch())
                        add(Severity::Error, "fullUrl", tr("fullUrl is not a valid urn:oid"), fullUrl);
                } else if (fullUrl.contains("/_history/")) {
                    add(Severity::Error, "fullUrl", tr("fullUrl must not be version specific"), fullUrl);
                } else if (!resource.type.isEmpty()) {
                    const auto match = restful.match(fullUrl);
                    if (!match.hasMatch())
                        add(Severity::Warning, "fullUrl", tr("fullUrl is neither a urn nor a RESTful Type/id URL"),
                            fullUrl);
                    else if (match.captured(1) != resource.type ||
                             (!resource.id.isEmpty() && match.captured(2) != resource.id))
                        add(Severity::Warning, "fullUrl",
                            tr("fullUrl does not end with %1/%2").arg(resource.type, resource.id), fullUrl);
                }
            }
            if (request) {
                const QJsonObject req = entry.value("request").toObject();
                if (!entry.contains("request"))
                    add(Severity::Error, "request", tr("Entries in a transaction or batch need a request"));
                else {
                    static const QStringList methods = {"GET", "HEAD", "POST", "PUT", "DELETE", "PATCH"};
                    if (!req.contains("method"))
                        add(Severity::Error, "request.method", tr("request.method is required"));
                    else if (!methods.contains(req.value("method").toString()))
                        add(Severity::Error, "request.method", tr("request.method is not a valid HTTP verb"),
                            req.value("method").toString());
                    if (!req.contains("url"))
                        add(Severity::Error, "request.url", tr("request.url is required"));
                }
            }
        }
    }

    void checkResource(const QJsonObject &json, const QString &path, bool topLevel) {
        const QJsonValue typeValue = json.value("resourceType");
        const QString type = typeValue.toString();
        if (!json.contains("resourceType")) {
            add(Severity::Error, path, tr("resourceType is missing"));
        } else if (!typeValue.isString() || !isKnownResourceType(type)) {
            add(Severity::Error, childPath(path, "resourceType"), tr("resourceType is not a FHIR R4 resource type"),
                typeValue.isString() ? type : tr("(not a string)"));
        }
        if (json.contains("id")) {
            if (!json.value("id").isString() || !isValidId(json.value("id").toString()))
                add(Severity::Error, childPath(path, "id"),
                    tr("id must be 1-64 letters, digits, '-' or '.'"), json.value("id").toString());
        } else if (!topLevel) {
            add(Severity::Error, path, tr("A contained resource needs an id"));
        }
        checkElement(json, path, QString(), {});
        if (!topLevel)
            return;
        m_type = type;
        Rules rules{*this, json, path};
        rules.check(type);
    }

    // The walk over every element: JSON shapes FHIR forbids, primitive
    // formats, codes, quantities, and references.
    void checkElement(const QJsonObject &object, const QString &path, const QString &key,
                      const QSet<QString> &skip) {
        if (object.isEmpty()) {
            add(Severity::Error, path, tr("Empty objects are not allowed in FHIR JSON"));
            return;
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (skip.contains(it.key()) || it.key() == "resourceType")
                continue;
            // _name carries the id and extensions of primitive `name`.
            if (it.key().startsWith('_'))
                continue;
            if (it.key() == "contained") {
                const QJsonArray contained = it.value().toArray();
                for (int i = 0; i < contained.size(); ++i)
                    checkResource(contained[i].toObject(), indexPath(childPath(path, "contained"), i), false);
                continue;
            }
            checkValue(it.value(), it.key(), childPath(path, it.key()), key);
        }
        if (object.contains("reference") && object.value("reference").isString())
            checkReference(object.value("reference").toString(), childPath(path, "reference"));
        // A Coding's code and system.
        if (object.value("code").isString() && (object.contains("system") || object.contains("display")) &&
            !isValidCode(object.value("code").toString()))
            add(Severity::Error, childPath(path, "code"), tr("code has leading, trailing, or repeated whitespace"),
                object.value("code").toString());
        if (object.value("system").isString() && object.value("system").toString().contains(QRegularExpression("\\s")))
            add(Severity::Error, childPath(path, "system"), tr("system must be a URI without whitespace"),
                object.value("system").toString());
        if (key.endsWith("Quantity") && object.contains("value") && !object.value("value").isDouble())
            add(Severity::Error, childPath(path, "value"), tr("Quantity.value must be a JSON number"),
                object.value("value").toVariant().toString());
    }

    void checkValue(const QJsonValue &value, const QString &key, const QString &path, const QString &parentKey) {
        switch (value.type()) {
        case QJsonValue::Null:
            add(Severity::Error, path, tr("null is not allowed in FHIR JSON; leave the element out"));
            return;
        case QJsonValue::String: {
            const QString text = value.toString();
            if (text.isEmpty()) {
                add(Severity::Error, path, tr("Empty strings are not allowed in FHIR JSON"));
                return;
            }
            if (key != "div" && text.trimmed().isEmpty())
                add(Severity::Error, path, tr("Strings must have non-whitespace content"));
            checkFormat(text, key, path, parentKey);
            return;
        }
        case QJsonValue::Array: {
            const QJsonArray array = value.toArray();
            if (array.isEmpty()) {
                add(Severity::Error, path, tr("Empty arrays are not allowed in FHIR JSON"));
                return;
            }
            for (int i = 0; i < array.size(); ++i) {
                if (array[i].isNull()) {
                    add(Severity::Error, indexPath(path, i), tr("null is not allowed in arrays"));
                    continue;
                }
                checkValue(array[i], key, indexPath(path, i), parentKey);
            }
            return;
        }
        case QJsonValue::Object:
            checkElement(value.toObject(), path, key, {});
            return;
        default:
            if (formatOf(key, parentKey) != Format::None)
                add(Severity::Error, path, tr("%1 must be a string").arg(key));
            return;
        }
    }

    void checkFormat(const QString &text, const QString &key, const QString &path, const QString &parentKey) {
        switch (formatOf(key, parentKey)) {
        case Format::Date:
            if (!isValidDate(text))
                add(Severity::Error, path, tr("%1 is not a valid FHIR date (YYYY, YYYY-MM, or YYYY-MM-DD)").arg(key),
                    text);
            break;
        case Format::DateTime:
            if (!isValidDateTime(text))
                add(Severity::Error, path,
                    tr("%1 is not a valid FHIR dateTime (a date, or a date and time with a time zone)").arg(key),
                    text);
            break;
        case Format::Instant:
            if (!isValidInstant(text))
                add(Severity::Error, path,
                    tr("%1 is not a valid FHIR instant (date, time to the second, and time zone)").arg(key), text);
            break;
        case Format::None:
            break;
        }
    }

    void checkReference(const QString &reference, const QString &path) {
        if (reference.isEmpty())
            return; // Reported as an empty string.
        if (reference.contains(QRegularExpression("\\s"))) {
            add(Severity::Error, path, tr("Reference contains whitespace"), reference);
            return;
        }
        if (reference.startsWith('#')) {
            if (reference.size() > 1 && !m_contained.contains(reference.mid(1)))
                add(Severity::Error, path, tr("Reference points at a contained resource that isn't there"), reference);
            return;
        }
        if (reference.contains('?')) {
            add(Severity::Info, path, tr("Conditional reference; resolved by the server, not checked here"), reference);
            return;
        }
        const bool inFile = m_file.kind == File::Kind::Bundle || m_file.kind == File::Kind::Ndjson;
        const int target = m_file.resolve(reference);
        if (reference.startsWith("urn:")) {
            if (target < 0)
                add(Severity::Error, path,
                    inFile ? tr("Reference does not resolve to any fullUrl in this file")
                           : tr("A urn reference can only resolve inside a Bundle"),
                    reference);
            return;
        }
        static const QRegularExpression relative(R"(^([A-Za-z]+)/([^/]+)(/_history/[^/]+)?$)");
        const auto match = relative.match(reference);
        if (match.hasMatch()) {
            if (!isKnownResourceType(match.captured(1))) {
                add(Severity::Error, path, tr("%1 is not a resource type").arg(match.captured(1)), reference);
                return;
            }
            if (!isValidId(match.captured(2))) {
                add(Severity::Error, path, tr("Reference has an invalid id"), reference);
                return;
            }
        } else if (!reference.startsWith("http://") && !reference.startsWith("https://")) {
            add(Severity::Error, path, tr("Reference is neither Type/id, an absolute URL, a urn, nor #contained"),
                reference);
            return;
        }
        if (target >= 0 || !inFile)
            return;
        if (m_file.kind == File::Kind::Bundle && match.hasMatch())
            add(Severity::Warning, path, tr("Reference does not resolve within this Bundle"), reference);
        else
            add(Severity::Info, path, tr("Reference is not in this file"), reference);
    }

    // Required elements, their JSON shapes, and required code sets for the
    // core resource types.
    struct Rules {
        Validator &v;
        const QJsonObject &json;
        const QString &path;

        QString at(const QString &key) const { return childPath(path, key); }

        void require(const QString &key) const {
            if (!json.contains(key))
                v.add(Severity::Error, path, tr("%1.%2 is required").arg(v.m_type, key));
        }
        void requireChoice(const QString &stem, const QStringList &types) const {
            for (const auto &type : types)
                if (json.contains(stem + type))
                    return;
            v.add(Severity::Error, path, tr("%1.%2[x] is required").arg(v.m_type, stem));
        }
        void string(const QString &key) const {
            if (json.contains(key) && !json.value(key).isString())
                v.add(Severity::Error, at(key), tr("%1 must be a string").arg(key));
        }
        void object(const QString &key) const {
            if (json.contains(key) && !json.value(key).isObject())
                v.add(Severity::Error, at(key), tr("%1 must be a JSON object").arg(key));
        }
        void array(const QStringList &keys) const {
            for (const auto &key : keys)
                if (json.contains(key) && !json.value(key).isArray())
                    v.add(Severity::Error, at(key), tr("%1 must be an array").arg(key));
        }
        void code(const QString &key, const QStringList &allowed) const {
            string(key);
            const QString value = json.value(key).toString();
            if (json.value(key).isString() && !value.isEmpty() && !allowed.contains(value))
                v.add(Severity::Error, at(key), tr("%1 is not one of: %2").arg(key, allowed.join(", ")), value);
        }
        void codes(const QString &key, const QStringList &allowed) const {
            const QJsonArray values = json.value(key).toArray();
            for (int i = 0; i < values.size(); ++i)
                if (values[i].isString() && !allowed.contains(values[i].toString()))
                    v.add(Severity::Error, indexPath(at(key), i),
                          tr("%1 is not one of: %2").arg(key, allowed.join(", ")), values[i].toString());
        }
        void requireChoiceIn(const QString &list, const QString &stem, const QStringList &types) const {
            const QJsonArray items = json.value(list).toArray();
            for (int i = 0; i < items.size(); ++i) {
                if (!items[i].isObject())
                    continue;
                bool found = false;
                for (const auto &type : types)
                    found = found || items[i].toObject().contains(stem + type);
                if (!found)
                    v.add(Severity::Error, indexPath(at(list), i),
                          tr("%1.%2.%3[x] is required").arg(v.m_type, list, stem));
            }
        }
        void requireIn(const QString &list, const QString &key) const {
            const QJsonArray items = json.value(list).toArray();
            for (int i = 0; i < items.size(); ++i)
                if (items[i].isObject() && !items[i].toObject().contains(key))
                    v.add(Severity::Error, indexPath(at(list), i),
                          tr("%1.%2.%3 is required").arg(v.m_type, list, key));
        }

        void check(const QString &type) const {
            array({"extension", "modifierExtension", "contained"});
            object("meta");
            object("text");
            if (type == "Patient")
                patient();
            else if (type == "Encounter")
                encounter();
            else if (type == "Observation")
                observation();
            else if (type == "Condition")
                condition();
            else if (type == "MedicationRequest")
                medicationRequest();
            else if (type == "Procedure")
                procedure();
            else if (type == "AllergyIntolerance")
                allergyIntolerance();
            else if (type == "Immunization")
                immunization();
            else if (type == "DiagnosticReport")
                diagnosticReport();
        }

        void patient() const {
            array({"identifier", "name", "telecom", "address", "contact", "communication", "link",
                   "generalPractitioner", "photo"});
            code("gender", {"male", "female", "other", "unknown"});
            string("birthDate");
            requireIn("communication", "language");
            requireIn("link", "other");
            requireIn("link", "type");
            if (!json.contains("name") && !json.contains("identifier"))
                v.add(Severity::Info, path, tr("Patient has neither a name nor an identifier"));
        }
        void encounter() const {
            require("status");
            require("class");
            code("status", {"planned", "arrived", "triaged", "in-progress", "onleave", "finished", "cancelled",
                            "entered-in-error", "unknown"});
            object("class");
            object("subject");
            object("period");
            array({"identifier", "type", "participant", "reasonCode", "reasonReference", "diagnosis",
                   "statusHistory", "location"});
            requireIn("diagnosis", "condition");
            requireIn("location", "location");
        }
        void observation() const {
            require("status");
            require("code");
            code("status", {"registered", "preliminary", "final", "amended", "corrected", "cancelled",
                            "entered-in-error", "unknown"});
            object("code");
            object("subject");
            array({"identifier", "category", "performer", "component", "interpretation", "note",
                   "referenceRange", "hasMember", "derivedFrom", "basedOn", "partOf"});
            requireIn("component", "code");
            bool hasValue = false;
            for (auto it = json.begin(); it != json.end(); ++it)
                hasValue = hasValue || it.key().startsWith("value");
            if (!hasValue && !json.contains("dataAbsentReason") && !json.contains("component") &&
                !json.contains("hasMember"))
                v.add(Severity::Info, path, tr("Observation has no value, component, member, or dataAbsentReason"));
        }
        void condition() const {
            require("subject");
            object("subject");
            object("code");
            object("clinicalStatus");
            object("verificationStatus");
            array({"identifier", "category", "bodySite", "stage", "evidence", "note"});
            if (!json.contains("code"))
                v.add(Severity::Warning, path, tr("Condition has no code, so it doesn't say what the condition is"));
        }
        void medicationRequest() const {
            require("status");
            require("intent");
            require("subject");
            requireChoice("medication", {"CodeableConcept", "Reference"});
            code("status", {"active", "on-hold", "cancelled", "completed", "entered-in-error", "stopped",
                            "draft", "unknown"});
            code("intent", {"proposal", "plan", "order", "original-order", "reflex-order", "filler-order",
                            "instance-order", "option"});
            code("priority", {"routine", "urgent", "asap", "stat"});
            object("subject");
            array({"identifier", "category", "dosageInstruction", "reasonCode", "reasonReference", "note",
                   "basedOn"});
        }
        void procedure() const {
            require("status");
            require("subject");
            code("status", {"preparation", "in-progress", "not-done", "on-hold", "stopped", "completed",
                            "entered-in-error", "unknown"});
            object("code");
            object("subject");
            array({"identifier", "performer", "reasonCode", "reasonReference", "bodySite", "note",
                   "complication", "focalDevice", "usedReference", "usedCode"});
            requireIn("performer", "actor");
        }
        void allergyIntolerance() const {
            require("patient");
            object("patient");
            code("type", {"allergy", "intolerance"});
            codes("category", {"food", "medication", "environment", "biologic"});
            code("criticality", {"low", "high", "unable-to-assess"});
            array({"identifier", "category", "reaction", "note"});
            requireIn("reaction", "manifestation");
        }
        void immunization() const {
            require("status");
            require("vaccineCode");
            require("patient");
            requireChoice("occurrence", {"DateTime", "String"});
            code("status", {"completed", "entered-in-error", "not-done"});
            object("vaccineCode");
            object("patient");
            array({"identifier", "performer", "note", "reasonCode", "reasonReference", "reaction",
                   "protocolApplied"});
            requireIn("performer", "actor");
            requireChoiceIn("protocolApplied", "doseNumber", {"PositiveInt", "String"});
        }
        void diagnosticReport() const {
            require("status");
            require("code");
            code("status", {"registered", "partial", "preliminary", "final", "amended", "corrected", "appended",
                            "cancelled", "entered-in-error", "unknown"});
            object("code");
            object("subject");
            array({"identifier", "basedOn", "category", "performer", "result", "media", "conclusionCode",
                   "presentedForm", "specimen"});
            requireIn("media", "link");
        }
    };

    const File &m_file;
    QList<Issue> m_issues;
    int m_index = -1;
    QString m_base, m_type;
    QSet<QString> m_contained;
};

} // namespace

QList<Issue> validate(const File &file) {
    return Validator(file).run();
}

} // namespace fhir
