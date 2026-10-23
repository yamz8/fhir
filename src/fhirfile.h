#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

namespace fhir {

enum class Severity { Error, Warning, Info };
QString severityName(Severity severity);

// One finding. The path is relative to what the Resources pane shows for the
// resource (the Bundle entry, or the resource itself), so it can be both
// printed and highlighted. The offending value is kept apart from the message
// so redaction can hide it.
struct Issue {
    Severity severity = Severity::Error;
    int resource = -1; // -1 for the file as a whole
    QString path;
    QString message;
    QString value;
};

struct Resource {
    QJsonObject json;       // the resource itself
    QJsonObject shown;      // the Bundle entry around it, or the resource
    QString resourcePath;   // where json sits inside shown: "resource" or ""
    QString type, id, fullUrl;
    int entry = -1;         // index in Bundle.entry
    int line = 0;           // line in an NDJSON file
    bool container = false; // the Bundle that holds the entries
};

// A FHIR R4 JSON file: one resource, a Bundle, or NDJSON bulk export. The
// source is only ever read.
class File {
public:
    enum class Kind { Empty, Single, Bundle, Ndjson };

    bool load(const QString &path);
    void loadData(const QByteArray &data, const QString &name);

    Kind kind = Kind::Empty;
    QString path, error;
    QList<Resource> resources;
    QList<Issue> parseIssues;

    // The resource a Reference.reference points at by fullUrl or Type/id, or
    // -1. Contained (#id) references are the validator's to check.
    int resolve(const QString &reference) const;
    int indexOfFullUrl(const QString &fullUrl) const { return m_byUrl.value(fullUrl, -1); }
    int indexOfTypeId(const QString &type, const QString &id) const { return m_byTypeId.value(type + "/" + id, -1); }
    // The Patient a resource is about: itself, or its subject or patient.
    int patientOf(int index) const;
    QString patientReference(int index) const;
    // Where an issue is, for people: "Bundle.entry[3].resource.code", "line 4: Observation.code".
    QString location(const Issue &issue) const;
    QString label(int index) const; // "Observation/abc"
    int entryCount() const;

private:
    void reset();
    void addParseError(const QJsonParseError &error, const QByteArray &data, int line, int base);
    void buildIndex();

    QHash<QString, int> m_byUrl, m_byTypeId;
};

bool isKnownResourceType(const QString &type);
QStringList coreResourceTypes();

QString childPath(const QString &path, const QString &key);
QString indexPath(const QString &path, int index);

// The readable text of a CodeableConcept: its text, a coding display, or a code.
QString conceptText(const QJsonValue &value);
// "system|code" for every coding, LOINC first.
QStringList conceptCodes(const QJsonValue &value);
QString patientName(const QJsonObject &patient);

} // namespace fhir
