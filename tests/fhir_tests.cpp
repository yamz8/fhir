#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "cli.h"
#include "fhirfile.h"
#include "jsonview.h"
#include "redact.h"
#include "report.h"
#include "theme.h"
#include "timeline.h"
#include "translator.h"
#include "trendchart.h"
#include "validator.h"

using namespace fhir;

// Every bundle here is synthetic. The people are "Test Patient" and
// "Demo Patient", and no field comes from a real record.
namespace {

const QString kTestPatient = "urn:uuid:00000000-0000-4000-8000-000000000001";
const QString kDemoPatient = "urn:uuid:00000000-0000-4000-8000-000000000002";

QString uuid(int n) {
    return QString("urn:uuid:00000000-0000-4000-8000-%1").arg(n, 12, 10, QChar('0'));
}

QJsonObject coded(const QString &system, const QString &code, const QString &display, const QString &text = {}) {
    QJsonObject result{{"coding", QJsonArray{QJsonObject{{"system", system}, {"code", code}, {"display", display}}}}};
    if (!text.isEmpty())
        result.insert("text", text);
    return result;
}

QJsonObject testPatient(const QString &id = "test-patient", const QString &given = "Test") {
    return {{"resourceType", "Patient"},
            {"id", id},
            {"identifier", QJsonArray{QJsonObject{{"system", "urn:example:mrn"}, {"value", "MRN-" + id}}}},
            {"name", QJsonArray{QJsonObject{{"family", "Patient"}, {"given", QJsonArray{given}}}}},
            {"telecom", QJsonArray{QJsonObject{{"system", "phone"}, {"value", "555-0100"}}}},
            {"address", QJsonArray{QJsonObject{{"city", "Sampletown"}, {"line", QJsonArray{"1 Example Street"}}}}},
            {"gender", "female"},
            {"birthDate", "1970-01-01"}};
}

QJsonObject observation(const QString &subject, const QString &date, double value, const QString &code = "4548-4",
                        const QString &text = "Hemoglobin A1c", const QString &unit = "%") {
    return {{"resourceType", "Observation"},
            {"id", "obs-" + date.left(10)},
            {"status", "final"},
            {"code", coded("http://loinc.org", code, text + " (LOINC)", text)},
            {"subject", QJsonObject{{"reference", subject}}},
            {"effectiveDateTime", date},
            {"valueQuantity", QJsonObject{{"value", value}, {"unit", unit}}}};
}

QJsonObject entry(const QString &fullUrl, const QJsonObject &resource) {
    return {{"fullUrl", fullUrl}, {"resource", resource}};
}

QJsonObject bundle(const QJsonArray &entries, const QString &type = "collection") {
    return {{"resourceType", "Bundle"}, {"id", "synthetic"}, {"type", type}, {"entry", entries}};
}

File loaded(const QJsonObject &json, const QString &name = "synthetic.json") {
    File file;
    file.loadData(QJsonDocument(json).toJson(), name);
    return file;
}

bool has(const QList<Issue> &issues, Severity severity, const QString &path, const QString &message) {
    for (const auto &issue : issues)
        if (issue.severity == severity && issue.path == path && issue.message.contains(message))
            return true;
    return false;
}

QString dump(const QList<Issue> &issues) {
    QStringList lines;
    for (const auto &issue : issues)
        lines << severityName(issue.severity) + " " + issue.path + ": " + issue.message + " " + issue.value;
    return lines.join('\n');
}

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

} // namespace

class FhirTests : public QObject {
    Q_OBJECT

private slots:
    void requiredFieldsAreErrors()
    {
        QJsonObject noStatus = observation(kTestPatient, "2024-01-01", 7.0);
        noStatus.remove("status");
        noStatus.remove("code");
        const QJsonObject encounter{{"resourceType", "Encounter"}, {"id", "e1"}, {"status", "finished"},
                                    {"subject", QJsonObject{{"reference", kTestPatient}}}};
        const QJsonObject medication{{"resourceType", "MedicationRequest"}, {"id", "m1"}, {"status", "active"},
                                     {"intent", "order"}, {"subject", QJsonObject{{"reference", kTestPatient}}}};
        const QJsonObject immunization{{"resourceType", "Immunization"}, {"id", "i1"}, {"status", "completed"},
                                       {"patient", QJsonObject{{"reference", kTestPatient}}}};
        const File file = loaded(bundle({entry(kTestPatient, testPatient()), entry(uuid(10), noStatus),
                                         entry(uuid(11), encounter), entry(uuid(12), medication),
                                         entry(uuid(13), immunization)}));
        const QList<Issue> issues = validate(file);
        QVERIFY2(has(issues, Severity::Error, "resource", "Observation.status is required"), qPrintable(dump(issues)));
        QVERIFY(has(issues, Severity::Error, "resource", "Observation.code is required"));
        QVERIFY(has(issues, Severity::Error, "resource", "Encounter.class is required"));
        QVERIFY(has(issues, Severity::Error, "resource", "MedicationRequest.medication[x] is required"));
        QVERIFY(has(issues, Severity::Error, "resource", "Immunization.vaccineCode is required"));
        QVERIFY(has(issues, Severity::Error, "resource", "Immunization.occurrence[x] is required"));
        QCOMPARE(file.location(issues.first()).left(15), QString("Bundle.entry[1]"));
    }

    void codesAndShapesAreChecked()
    {
        QJsonObject procedure{{"resourceType", "Procedure"}, {"id", "p1"}, {"status", "done"},
                              {"subject", QJsonObject{{"reference", kTestPatient}}}};
        QJsonObject patient = testPatient();
        patient.insert("gender", "f");
        patient.insert("name", QJsonObject{{"family", "Patient"}});
        patient.insert("photo", QJsonArray());
        patient.insert("id", "not valid!");
        const QList<Issue> issues =
            validate(loaded(bundle({entry(kTestPatient, patient), entry(uuid(20), procedure)})));
        QVERIFY2(has(issues, Severity::Error, "resource.status", "status is not one of"), qPrintable(dump(issues)));
        QVERIFY(has(issues, Severity::Error, "resource.gender", "gender is not one of"));
        QVERIFY(has(issues, Severity::Error, "resource.name", "name must be an array"));
        QVERIFY(has(issues, Severity::Error, "resource.photo", "Empty arrays are not allowed"));
        QVERIFY(has(issues, Severity::Error, "resource.id", "id must be 1-64"));
    }

    void dateFormatsAreChecked()
    {
        QJsonObject patient = testPatient();
        patient.insert("birthDate", "1970-13-01");
        QJsonObject feb30 = observation(kTestPatient, "2024-02-30", 7.0);
        QJsonObject noZone = observation(kTestPatient, "2024-03-01T10:00:00", 7.1);
        noZone.insert("id", "obs-nozone");
        QJsonObject issuedDate = observation(kTestPatient, "2024-04-01T10:00:00+02:00", 7.2);
        issuedDate.insert("issued", "2024-04-01");
        QJsonObject good = observation(kTestPatient, "2024-05", 7.3);
        good.insert("issued", "2024-05-02T08:30:00.123Z");
        const File file = loaded(bundle({entry(kTestPatient, patient), entry(uuid(30), feb30), entry(uuid(31), noZone),
                                         entry(uuid(32), issuedDate), entry(uuid(33), good)}));
        const QList<Issue> issues = validate(file);
        QVERIFY2(has(issues, Severity::Error, "resource.birthDate", "not a valid FHIR date"), qPrintable(dump(issues)));
        int dateErrors = 0;
        for (const auto &issue : issues)
            dateErrors += issue.message.contains("not a valid FHIR");
        QCOMPARE(dateErrors, 4);
        QVERIFY(has(issues, Severity::Error, "resource.issued", "not a valid FHIR instant"));
        for (const auto &issue : issues)
            QVERIFY2(issue.resource != 5, qPrintable(dump(issues)));

        QVERIFY(isValidDate("2024"));
        QVERIFY(isValidDate("2024-02-29"));
        QVERIFY(!isValidDate("2023-02-29"));
        QVERIFY(isValidDateTime("2024-01-01T00:00:00Z"));
        QVERIFY(isValidDateTime("2024-01-01T23:59:60.5-05:00"));
        QVERIFY(!isValidDateTime("2024-01-01T10:00Z"));
        QVERIFY(isValidInstant("2024-01-01T10:00:00+14:00"));
        QVERIFY(!isValidInstant("2024-01-01T10:00:00"));
    }

    void referencesMustResolve()
    {
        QJsonObject broken = observation("urn:uuid:00000000-0000-4000-8000-999999999999", "2024-01-01", 7.0);
        QJsonObject relative = observation("Patient/missing", "2024-01-02", 7.1);
        relative.insert("id", "obs-relative");
        QJsonObject contained = observation(kTestPatient, "2024-01-03", 7.2);
        contained.insert("id", "obs-contained");
        contained.insert("performer", QJsonArray{QJsonObject{{"reference", "#nobody"}}});
        QJsonObject good = observation("Patient/test-patient", "2024-01-04", 7.3);
        good.insert("id", "obs-good");
        const File file = loaded(bundle({entry(kTestPatient, testPatient()), entry(uuid(40), broken),
                                         entry(uuid(41), relative), entry(uuid(42), contained),
                                         entry(uuid(43), good), entry(uuid(43), observation(kTestPatient, "2024-01-05", 1))}));
        const QList<Issue> issues = validate(file);
        QVERIFY2(has(issues, Severity::Error, "resource.subject.reference", "does not resolve to any fullUrl"),
                 qPrintable(dump(issues)));
        // A collection may point at a server: counted once, not warned about.
        QVERIFY(has(issues, Severity::Info, "", "References to resources outside this file, not checked: 1"));
        QVERIFY(has(issues, Severity::Error, "resource.performer[0].reference", "contained resource that isn't there"));
        QVERIFY(has(issues, Severity::Error, "fullUrl", "fullUrl is also used by entry 4"));
        for (const auto &issue : issues)
            QVERIFY2(issue.resource != 5 || issue.path != "resource.subject.reference", qPrintable(dump(issues)));
        QCOMPARE(file.resolve("Patient/test-patient"), 1);
        QCOMPARE(file.resolve(kTestPatient), 1);
        QCOMPARE(file.patientOf(5), 1);
    }

    void unknownTypesAndBadJson()
    {
        const File unknown = loaded(QJsonObject{{"resourceType", "Patinet"}, {"id", "x"}});
        QVERIFY(has(validate(unknown), Severity::Error, "resourceType", "not a FHIR R4 resource type"));
        const File missing = loaded(QJsonObject{{"id", "x"}});
        QVERIFY(has(validate(missing), Severity::Error, "", "resourceType is missing"));

        File empty;
        empty.loadData("  \n", "empty.json");
        QVERIFY(has(validate(empty), Severity::Error, "", "The file is empty"));

        File bad;
        bad.loadData("{\n  \"resourceType\": \"Patient\",\n  \"id\": \n}", "bad.json");
        const QList<Issue> issues = validate(bad);
        QCOMPARE(issues.size(), 1);
        QVERIFY2(issues.first().message.startsWith("Invalid JSON at line 4"), qPrintable(issues.first().message));
    }

    void ndjsonParsesLineByLine()
    {
        QByteArray data;
        data += QJsonDocument(testPatient()).toJson(QJsonDocument::Compact) + "\n";
        data += QJsonDocument(observation("Patient/test-patient", "2024-01-02", 7.1)).toJson(QJsonDocument::Compact) + "\n";
        data += "{\"resourceType\": \"Observation\", oops}\n";
        data += "\n";
        data += QJsonDocument(observation("Patient/elsewhere", "2024-01-01", 7.0)).toJson(QJsonDocument::Compact) + "\n";
        File file;
        file.loadData(data, "Observation.ndjson");
        QCOMPARE(file.kind, File::Kind::Ndjson);
        QCOMPARE(file.resources.size(), 3);
        QCOMPARE(file.resources[2].line, 5);
        const QList<Issue> issues = validate(file);
        QVERIFY2(has(issues, Severity::Error, "", "Invalid JSON at line 3"), qPrintable(dump(issues)));
        // One line for every reference to another file, not one per reference.
        QVERIFY2(has(issues, Severity::Info, "", "References to resources outside this file, not checked: 1"), qPrintable(dump(issues)));
        QCOMPARE(file.patientOf(1), 0);
        QCOMPARE(file.location(Issue{Severity::Error, 1, "code", {}, {}}), QString("line 2: Observation.code"));

        // Without the .ndjson name, a file of JSON lines is still NDJSON.
        File unnamed;
        unnamed.loadData(data, "export.json");
        QCOMPARE(unnamed.kind, File::Kind::Ndjson);
    }

    void timelineIsGroupedAndOrdered()
    {
        QJsonObject late = observation(kTestPatient, "2024-01-01T23:00:00-05:00", 7.0); // 04:00Z on the 2nd
        late.insert("id", "late");
        QJsonObject early = observation(kTestPatient, "2024-01-02T01:00:00Z", 7.5);
        early.insert("id", "early");
        const QJsonObject condition{{"resourceType", "Condition"}, {"id", "c1"},
                                    {"code", coded("http://snomed.info/sct", "44054006", "Diabetes", "Type 2 diabetes")},
                                    {"subject", QJsonObject{{"reference", kTestPatient}}}, {"onsetDateTime", "2019"}};
        const File file = loaded(bundle({entry(kTestPatient, testPatient()),
                                         entry(kDemoPatient, testPatient("demo-patient", "Demo")),
                                         entry(uuid(50), observation(kDemoPatient, "2020-06-01", 6.1)), entry(uuid(51), late),
                                         entry(uuid(52), early), entry(uuid(53), condition),
                                         entry(uuid(54), observation("Patient/elsewhere", "2010-01-01", 5.0))}));
        const Timeline timeline = buildTimeline(file);
        QCOMPARE(timeline.groups.size(), 3);
        QCOMPARE(patientLabel(file, timeline, timeline.groups[0], false), QString("Test Patient"));
        QCOMPARE(patientLabel(file, timeline, timeline.groups[1], false), QString("Demo Patient"));
        QCOMPARE(timeline.groups[2].key, QString("Patient/elsewhere"));
        QStringList order;
        for (const int index : timeline.groups[0].events)
            order << file.resources[timeline.events[index].resource].id;
        QCOMPARE(order, QStringList({"c1", "early", "late"}));
        QCOMPARE(timeline.events[timeline.groups[0].events[0]].summary, QString("Type 2 diabetes"));
    }

    void summariesReadLikeAChart()
    {
        const QJsonObject metformin{
            {"resourceType", "MedicationRequest"}, {"id", "m1"}, {"status", "active"}, {"intent", "order"},
            {"subject", QJsonObject{{"reference", kTestPatient}}}, {"authoredOn", "2024-01-01"},
            {"medicationCodeableConcept", coded("http://www.nlm.nih.gov/research/umls/rxnorm", "861007",
                                                  "Metformin hydrochloride 500 MG Oral Tablet", "Metformin")},
            {"dosageInstruction",
             QJsonArray{QJsonObject{{"route", coded("http://snomed.info/sct", "26643006", "Oral route")},
                                    {"timing", QJsonObject{{"repeat", QJsonObject{{"frequency", 2}, {"period", 1},
                                                                                  {"periodUnit", "d"}}}}},
                                    {"doseAndRate", QJsonArray{QJsonObject{
                                                        {"doseQuantity", QJsonObject{{"value", 500}, {"unit", "mg"}}}}}}}}}};
        const QJsonObject pressure{
            {"resourceType", "Observation"}, {"id", "bp"}, {"status", "final"},
            {"code", coded("http://loinc.org", "85354-9", "Blood pressure panel", "Blood pressure")},
            {"subject", QJsonObject{{"reference", kTestPatient}}}, {"effectiveDateTime", "2024-01-01"},
            // Diastolic first and UCUM units, as Synthea writes them.
            {"component", QJsonArray{QJsonObject{{"code", coded("http://loinc.org", "8462-4", "Diastolic")},
                                                 {"valueQuantity", QJsonObject{{"value", 80}, {"unit", "mm[Hg]"}}}},
                                     QJsonObject{{"code", coded("http://loinc.org", "8480-6", "Systolic")},
                                                 {"valueQuantity", QJsonObject{{"value", 120}, {"unit", "mm[Hg]"}}}}}}};
        QJsonObject score = observation(kTestPatient, "2024-01-02", 3, "70274-6", "GAD-7 total score", "{score}");
        score.insert("id", "score");
        const File file = loaded(bundle({entry(kTestPatient, testPatient()), entry(uuid(60), metformin),
                                         entry(uuid(61), observation(kTestPatient, "2024-01-01", 7.2)),
                                         entry(uuid(62), pressure), entry(uuid(63), score)}));
        QStringList summaries;
        for (const auto &event : buildTimeline(file).events)
            summaries << event.summary;
        QVERIFY2(summaries.contains("Metformin 500 mg PO BID"), qPrintable(summaries.join(" | ")));
        QVERIFY(summaries.contains("Hemoglobin A1c 7.2 %"));
        QVERIFY(summaries.contains("Blood pressure 120/80 mmHg"));
        QVERIFY(summaries.contains("GAD-7 total score 3"));
        QVERIFY(validate(file).isEmpty());
    }

    void trendsNeedTwoReadings()
    {
        QJsonObject second = observation(kTestPatient, "2024-06-01", 6.8);
        QJsonObject weight = observation(kTestPatient, "2024-06-01", 80, "29463-7", "Body weight", "kg");
        weight.insert("id", "weight");
        const File file = loaded(bundle({entry(kTestPatient, testPatient()),
                                         entry(uuid(70), observation(kTestPatient, "2024-01-01", 7.2)),
                                         entry(uuid(71), second), entry(uuid(72), weight)}));
        const Timeline timeline = buildTimeline(file);
        const auto found = trends(timeline);
        QCOMPARE(found.size(), 1);
        const TrendSeries series = trendSeries(timeline, found.begin().value());
        QCOMPARE(series.points.size(), 2);
        QCOMPARE(series.title, QString("Hemoglobin A1c"));
        QCOMPARE(series.points.last().value, 6.8);
    }

    void filterNarrowsTheTimeline()
    {
        const QJsonObject condition{{"resourceType", "Condition"}, {"id", "c1"},
                                    {"code", coded("http://snomed.info/sct", "38341003", "Hypertensive disorder", "Hypertension")},
                                    {"subject", QJsonObject{{"reference", kTestPatient}}}, {"onsetDateTime", "2023-05-01"}};
        const File file = loaded(bundle({entry(kTestPatient, testPatient()),
                                         entry(uuid(80), observation(kTestPatient, "2024-01-01", 7.2)),
                                         entry(uuid(81), condition)}));
        const Timeline timeline = buildTimeline(file);
        auto count = [&](const Filter &filter) {
            int n = 0;
            for (const auto &event : timeline.events)
                n += filter.matches(file, event);
            return n;
        };
        Filter filter;
        QCOMPARE(count(filter), 2);
        filter.types = {"Condition"};
        QCOMPARE(count(filter), 1);
        filter = Filter();
        filter.from = QDate(2024, 1, 1);
        QCOMPARE(count(filter), 1);
        filter = Filter();
        filter.text = "38341003";
        QCOMPARE(count(filter), 1);
        filter.text = "hyperTENSION";
        QCOMPARE(count(filter), 1);
        filter = Filter();
        filter.patient = "test-patient";
        QCOMPARE(count(filter), 2);
    }

    void redactionMasksIdentifyingDetails()
    {
        QJsonObject withDisplay = observation(kTestPatient, "2024-01-01", 7.2);
        withDisplay.insert("subject", QJsonObject{{"reference", kTestPatient}, {"display", "Test Patient"}});
        const QJsonObject masked = redact(testPatient()).toObject();
        const QByteArray text = QJsonDocument(masked).toJson();
        for (const char *secret : {"Test", "MRN-", "555-0100", "Sampletown", "1 Example Street", "1970-01-01"})
            QVERIFY2(!text.contains(secret), secret);
        QCOMPARE(masked.value("gender").toString(), QString("female"));
        QCOMPARE(masked.value("identifier").toArray().first().toObject().value("system").toString(),
                 QString("urn:example:mrn"));
        const QJsonObject observed = redact(withDisplay).toObject();
        QCOMPARE(observed.value("subject").toObject().value("display").toString(), QString(kRedacted));
        QCOMPARE(observed.value("code").toObject().value("text").toString(), QString("Hemoglobin A1c"));

        QJsonObject patient = testPatient();
        patient.insert("birthDate", "1970-13-01");
        const File file = loaded(bundle({entry(kTestPatient, patient), entry(uuid(90), withDisplay)}));
        const QList<Issue> issues = validate(file);
        const Timeline timeline = buildTimeline(file);
        const QByteArray csv = timelineCsv(file, timeline, Filter(), true);
        QVERIFY2(csv.contains("Patient 1"), csv.constData());
        QVERIFY(!csv.contains("Test Patient"));
        QVERIFY(timelineCsv(file, timeline, Filter(), false).contains("Test Patient"));
        const QByteArray report = QJsonDocument(issuesJson(file, issues, true)).toJson();
        QVERIFY2(!report.contains("1970-13-01"), report.constData());
        QVERIFY(QJsonDocument(issuesJson(file, issues, false)).toJson().contains("1970-13-01"));
        const QByteArray summary = QJsonDocument(summaryJson(file, issues, timeline, true)).toJson();
        QVERIFY(!summary.contains("Test Patient") && !summary.contains("1970-13-01"));
    }

    void jsonViewHighlightsPaths()
    {
        const JsonView view = formatJson(QJsonObject{{"resource", testPatient()}, {"fullUrl", kTestPatient}});
        QVERIFY(view.text.indexOf("\"fullUrl\"") < view.text.indexOf("\"resource\""));
        QVERIFY(view.text.indexOf("\"resourceType\"") < view.text.indexOf("\"address\""));
        const auto range = view.rangeOf("resource.name[0].family");
        QCOMPARE(view.text.mid(range.first, range.second - range.first), QString("\"family\": \"Patient\""));
        // A path that wasn't printed falls back to its nearest printed parent.
        QCOMPARE(view.rangeOf("resource.name[0].suffix"), view.rangeOf("resource.name[0]"));
    }

    void exportsAreWrittenWhole()
    {
        QTemporaryDir dir;
        const File file = loaded(bundle({entry(kTestPatient, testPatient()),
                                         entry(uuid(100), observation(kTestPatient, "2024-01-01", 7.2)),
                                         entry(uuid(101), observation(kTestPatient, "2024-06-01", 6.8))}));
        const Timeline timeline = buildTimeline(file);
        QString error;
        QVERIFY2(exportTimelineDocument(dir.filePath("t.pdf"), "pdf", file, timeline, Filter(), true, QLocale::c(), &error),
                 qPrintable(error));
        QVERIFY(exportTimelineDocument(dir.filePath("t.svg"), "svg", file, timeline, Filter(), true, QLocale::c(), &error));
        QFile pdf(dir.filePath("t.pdf")), svg(dir.filePath("t.svg"));
        QVERIFY(pdf.open(QIODevice::ReadOnly) && pdf.read(4) == "%PDF");
        QVERIFY(svg.open(QIODevice::ReadOnly));
        const QByteArray drawing = svg.readAll();
        QVERIFY(drawing.contains("<svg") && drawing.contains("Patient 1") && !drawing.contains("Test Patient"));
        QVERIFY(saveAtomically(dir.filePath("r.csv"), issuesCsv(file, validate(file), false), &error));
        QVERIFY(!saveAtomically(dir.filePath("missing/r.csv"), "x", &error));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 3);
    }

    void backendNeverWritesOverTheSource()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.json");
        const QByteArray original = QJsonDocument(bundle({entry(kTestPatient, testPatient())})).toJson();
        write(source, original);
        Backend backend;
        QVERIFY(backend.openPath(source));
        QVERIFY(!backend.exportTo("issues-json", source));
        QVERIFY(backend.exportTo("issues-json", dir.filePath("report.json")));
        QFile file(source);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        backend.setRedact(true);
        QVERIFY(!backend.resourceView(1).value("text").toString().contains("Sampletown"));
        QVERIFY(!backend.openPath(dir.filePath("nothing.json")));
        QVERIFY(!backend.loadError().isEmpty());
    }

    void cliExitCodes()
    {
        QTemporaryDir dir;
        const QString clean = dir.filePath("clean.json"), broken = dir.filePath("broken.json"),
                      warned = dir.filePath("warned.json");
        write(clean, QJsonDocument(bundle({entry(kTestPatient, testPatient()),
                                           entry(uuid(110), observation(kTestPatient, "2024-01-01", 7.2))}))
                         .toJson());
        QJsonObject bad = observation(kTestPatient, "2024-02-30", 7.2);
        write(broken, QJsonDocument(bundle({entry(kTestPatient, testPatient()), entry(uuid(111), bad)})).toJson());
        // A document must carry what it references, so this is a warning.
        write(warned, QJsonDocument(bundle({entry(kTestPatient, testPatient()),
                                            entry(uuid(112), observation("Patient/elsewhere", "2024-01-01", 7))},
                                           "document"))
                          .toJson());
        QCOMPARE(runCli({"fhir", "check", clean, "--json"}), 0);
        QCOMPARE(runCli({"fhir", "check", broken, "--json"}), 1);
        QCOMPARE(runCli({"fhir", "check", broken}), 1);
        QCOMPARE(runCli({"fhir", "check", warned, "--json"}), 0);
        QCOMPARE(runCli({"fhir", "check", warned, "--json", "--strict"}), 1);
        QCOMPARE(runCli({"fhir", "check", dir.filePath("absent.json")}), 1);
        QCOMPARE(runCli({"fhir", "summary", clean, "--json"}), 0);
        QCOMPARE(runCli({"fhir", "timeline", clean, "--csv"}), 0);
        QCOMPARE(runCli({"fhir", "timeline", clean, "--patient", "test-patient", "--json"}), 0);
        QCOMPARE(runCli({"fhir", "timeline", clean, "--patient", "nobody", "--json"}), 1);
        QCOMPARE(runCli({"fhir", "timeline", clean, "--from", "January"}), 1);
        QCOMPARE(runCli({"fhir", "frobnicate"}), 1);
        QCOMPARE(runCli({"fhir"}), 0);
    }

    void oddTimingDoesNotCrash()
    {
        QJsonObject negative{{"resourceType", "MedicationRequest"}, {"id", "m1"}, {"status", "active"},
                             {"intent", "order"}, {"subject", QJsonObject{{"reference", kTestPatient}}},
                             {"authoredOn", "2024-01-01"},
                             {"medicationCodeableConcept", coded("http://www.nlm.nih.gov/research/umls/rxnorm", "1", "Drug", "Drug")},
                             {"dosageInstruction", QJsonArray{QJsonObject{{"timing", QJsonObject{{"repeat",
                                 QJsonObject{{"frequency", -3}, {"period", 1}, {"periodUnit", "d"}}}}}}}}};
        QJsonObject bare = negative;
        bare.insert("id", "m2");
        bare.remove("dosageInstruction");
        const QJsonObject encounter{{"resourceType", "Encounter"}, {"id", "e1"}, {"status", "finished"},
                                    {"class", QJsonObject{{"code", "AMB"}}}, {"reasonCode", QJsonArray()},
                                    {"subject", QJsonObject{{"reference", kTestPatient}}},
                                    {"period", QJsonObject{{"start", "2024-01-02"}}}};
        const File file = loaded(bundle({entry(kTestPatient, testPatient()), entry(uuid(120), negative),
                                         entry(uuid(121), bare), entry(uuid(122), encounter)}));
        QStringList summaries;
        for (const auto &event : buildTimeline(file).events)
            summaries << event.summary;
        QVERIFY2(summaries.contains("Drug -3×/d"), qPrintable(summaries.join(" | ")));
        QVERIFY(summaries.contains("Drug"));
        QVERIFY(summaries.contains("Encounter (AMB)"));
    }

    void summaryHasADateRange()
    {
        const File file = loaded(bundle({entry(kTestPatient, testPatient()),
                                         entry(uuid(130), observation(kTestPatient, "2024-06-01", 6.8)),
                                         entry(uuid(131), observation(kTestPatient, "2023-01-01", 7.2))}));
        const QJsonObject range = summaryJson(file, validate(file), buildTimeline(file), false)
                                      .value("dateRange").toObject();
        QCOMPARE(range.value("start").toString(), QString("2023-01-01"));
        QCOMPARE(range.value("end").toString(), QString("2024-06-01"));
    }

    void redactedPatientsAreNumberedOneWay()
    {
        // The first Patient has no dated events, so the timeline's first group is the second Patient.
        const File file = loaded(bundle({entry(kTestPatient, testPatient()),
                                         entry(kDemoPatient, testPatient("demo-patient", "Demo")),
                                         entry(uuid(140), observation(kDemoPatient, "2024-01-01", 7.2)),
                                         entry(uuid(141), observation("Patient/elsewhere", "2024-01-02", 7.0))}));
        const Timeline timeline = buildTimeline(file);
        const QJsonArray patients = summaryJson(file, {}, timeline, true).value("patients").toArray();
        QCOMPARE(patients[1].toObject().value("name").toString(), QString("Patient 2"));
        const QJsonArray groups = timelineJson(file, timeline, Filter(), true).value("patients").toArray();
        QCOMPARE(groups[0].toObject().value("patient").toString(), QString("Patient 2"));
        QCOMPARE(groups[1].toObject().value("patient").toString(), QString("Patient 3"));
        const QByteArray csv = timelineCsv(file, timeline, Filter(), true);
        QVERIFY2(csv.contains("Patient 2,2024-01-01") && !csv.contains("Patient 1,"), csv.constData());
        Backend backend;
        QTemporaryDir dir;
        write(dir.filePath("b.json"), QJsonDocument(bundle({entry(kTestPatient, testPatient()),
                                                            entry(kDemoPatient, testPatient("demo-patient", "Demo")),
                                                            entry(uuid(140), observation(kDemoPatient, "2024-01-01", 7.2))}))
                                          .toJson());
        QVERIFY(backend.openPath(dir.filePath("b.json")));
        backend.setRedact(true);
        QCOMPARE(backend.timelineRows().first().toMap().value("label").toString(), QString("Patient 2"));
        QCOMPARE(backend.eventDetail(0).value("patient").toString(), QString("Patient 2"));
        QStringList treeNames;
        for (const auto &row : backend.resourceRows())
            if (row.toMap().value("type") == "Patient" && !row.toMap().value("header").toBool())
                treeNames << row.toMap().value("summary").toString();
        QCOMPARE(treeNames, QStringList({"Patient 1", "Patient 2"}));
    }

    void redactionHidesFreeTextAndPersonIds()
    {
        const QString john = "Patient/john-smith";
        QJsonObject patient = testPatient("john-smith");
        QJsonObject noted = observation(john, "2024-01-01", 7.2);
        noted.insert("note", QJsonArray{QJsonObject{{"text", "Pt John Smith called from 555-1234"}}});
        QJsonObject stringValue = observation(john, "2024-01-02", 0);
        stringValue.remove("valueQuantity");
        stringValue.insert("id", "string-value");
        stringValue.insert("valueString", "Seen with John Smith's daughter");
        const QJsonObject report{{"resourceType", "DiagnosticReport"}, {"id", "r1"}, {"status", "final"},
                                 {"code", coded("http://loinc.org", "1", "Panel", "Panel")},
                                 {"subject", QJsonObject{{"reference", john}}}, {"effectiveDateTime", "2024-01-03"},
                                 {"conclusion", "John Smith is fine"}};
        const File file = loaded(bundle({entry("https://example.org/fhir/Patient/john-smith", patient),
                                         entry(uuid(150), noted), entry(uuid(151), stringValue), entry(uuid(152), report),
                                         entry(uuid(153), observation("Patient/john-missing", "2024-01-04", 1))},
                                        "document"));
        const QByteArray shown = QJsonDocument(redact(file.resources[2].shown).toObject()).toJson();
        QVERIFY2(!shown.contains("John") && !shown.contains("555-1234") && !shown.contains("john-smith"), shown.constData());
        QVERIFY(shown.contains("Patient/[redacted]"));
        QVERIFY(shown.contains("Hemoglobin A1c"));
        const QByteArray entryShown = QJsonDocument(redact(file.resources[1].shown).toObject()).toJson();
        QVERIFY2(!entryShown.contains("john-smith"), entryShown.constData());

        const Timeline timeline = buildTimeline(file);
        const QList<Issue> issues = validate(file);
        for (const QByteArray &out : {timelineCsv(file, timeline, Filter(), true),
                                      QJsonDocument(timelineJson(file, timeline, Filter(), true)).toJson(),
                                      QJsonDocument(summaryJson(file, issues, timeline, true)).toJson(),
                                      QJsonDocument(issuesJson(file, issues, true)).toJson(),
                                      issuesCsv(file, issues, true)})
            QVERIFY2(!out.contains("John") && !out.contains("john-"), out.constData());
        QVERIFY(timelineCsv(file, timeline, Filter(), false).contains("John Smith is fine"));
        QVERIFY(QJsonDocument(issuesJson(file, issues, false)).toJson().contains("Patient/john-missing"));
    }

    void csvNeverStartsAFormula()
    {
        const QJsonObject report{{"resourceType", "DiagnosticReport"}, {"id", "r1"}, {"status", "final"},
                                 {"code", coded("http://loinc.org", "1", "Panel", "-2+3+cmd|' /C calc'!A0")},
                                 {"subject", QJsonObject{{"reference", kTestPatient}}}, {"effectiveDateTime", "2024-01-03"}};
        const File file = loaded(bundle({entry(kTestPatient, testPatient()), entry(uuid(160), report)}));
        const QByteArray csv = timelineCsv(file, buildTimeline(file), Filter(), false);
        QVERIFY2(csv.contains(",\"'-2+3+cmd|' /C calc'!A0\",") || csv.contains(",'-2+3+cmd|' /C calc'!A0,"), csv.constData());
    }

    void validChoicesAndDatesAreAccepted()
    {
        QJsonObject immunization{{"resourceType", "Immunization"}, {"id", "i1"}, {"status", "completed"},
                                 {"vaccineCode", coded("http://hl7.org/fhir/sid/cvx", "115", "Tdap")},
                                 {"patient", QJsonObject{{"reference", kTestPatient}}},
                                 {"occurrenceDateTime", "2024-01-01"},
                                 {"expirationDate", "2025-01-01"},
                                 {"protocolApplied", QJsonArray{QJsonObject{{"doseNumberString", "booster"}},
                                                                QJsonObject{{"doseNumberPositiveInt", 2}},
                                                                QJsonObject{{"series", "none"}}}}};
        const QJsonObject medication{{"resourceType", "Medication"}, {"id", "med1"},
                                     {"batch", QJsonObject{{"lotNumber", "L1"}, {"expirationDate", "2025-01-01T00:00:00Z"}}}};
        QJsonObject lateImmunization = immunization;
        lateImmunization.insert("id", "i2");
        lateImmunization.insert("expirationDate", "2025-01-01T00:00:00Z");
        lateImmunization.remove("protocolApplied");
        const File file = loaded(bundle({entry(kTestPatient, testPatient()), entry(uuid(170), immunization),
                                         entry(uuid(171), medication), entry(uuid(172), lateImmunization)}));
        const QList<Issue> issues = validate(file);
        QCOMPARE(issues.size(), 2);
        QVERIFY2(has(issues, Severity::Error, "resource.protocolApplied[2]", "doseNumber[x] is required"), qPrintable(dump(issues)));
        QVERIFY(has(issues, Severity::Error, "resource.expirationDate", "not a valid FHIR date"));
        QCOMPARE(issues.last().resource, 4);
    }

    void syntheaShapesAreAccepted()
    {
        // Synthea writes transaction bundles with Device.expirationDate as a
        // dateTime and many conditional references to Locations and Practitioners.
        const QJsonObject device{{"resourceType", "Device"}, {"id", "d1"},
                                 {"expirationDate", "2042-05-17T08:01:15+00:00"},
                                 {"patient", QJsonObject{{"reference", kTestPatient}}}};
        QJsonObject encounter{{"resourceType", "Encounter"}, {"id", "e1"}, {"status", "finished"},
                              {"class", QJsonObject{{"code", "AMB"}}},
                              {"subject", QJsonObject{{"reference", kTestPatient}}},
                              {"location", QJsonArray{QJsonObject{{"location", QJsonObject{{"reference", "Location?identifier=x|1"}}}}}},
                              {"serviceProvider", QJsonObject{{"reference", "Organization?identifier=x|2"}}}};
        QJsonArray entries{entry(kTestPatient, testPatient()), entry(uuid(200), device), entry(uuid(201), encounter)};
        for (int i = 0; i < entries.size(); ++i) {
            QJsonObject e = entries[i].toObject();
            e.insert("request", QJsonObject{{"method", "POST"}, {"url", e.value("resource").toObject().value("resourceType")}});
            entries[i] = e;
        }
        const QList<Issue> issues = validate(loaded(bundle(entries, "transaction")));
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().severity, Severity::Info);
        QCOMPARE(issues.first().resource, -1);
        QVERIFY2(issues.first().message.endsWith("left for the server to resolve: 2"), qPrintable(dump(issues)));
    }

    void hl7ExampleShapesAreAccepted()
    {
        // Shapes from the official R4 examples that are valid and must not be reported.
        QJsonObject contract{{"resourceType", "Contract"}, {"id", "c1"}, {"issued", "2015-11-18"}};
        QJsonObject v1 = testPatient(), v2 = testPatient();
        v1.insert("meta", QJsonObject{{"versionId", "1"}});
        v2.insert("meta", QJsonObject{{"versionId", "2"}});
        const QJsonObject versions = bundle({entry("http://example.org/fhir/Patient/test-patient", v1),
                                             entry("http://example.org/fhir/Patient/test-patient", v2),
                                             entry(uuid(220), contract)});
        QList<Issue> issues = validate(loaded(versions));
        QVERIFY2(issues.isEmpty(), qPrintable(dump(issues)));

        // Reads in a transaction carry no resource.
        const QJsonObject reads{{"resourceType", "Bundle"}, {"type", "batch"},
                                {"entry", QJsonArray{QJsonObject{{"request", QJsonObject{{"method", "GET"}, {"url", "Patient/1"}}}}}}};
        issues = validate(loaded(reads));
        QVERIFY2(issues.isEmpty(), qPrintable(dump(issues)));

        // Responses may leave fullUrl out.
        const QJsonObject response{{"resourceType", "Bundle"}, {"type", "batch-response"},
                                   {"entry", QJsonArray{QJsonObject{{"resource", testPatient()},
                                                                    {"response", QJsonObject{{"status", "200 OK"}}}}}}};
        issues = validate(loaded(response));
        QVERIFY2(issues.isEmpty(), qPrintable(dump(issues)));

        // A document, unlike a collection, must contain what it references.
        QJsonObject document = bundle({entry(kTestPatient, testPatient()),
                                       entry(uuid(221), observation("Patient/elsewhere", "2024-01-01", 7))},
                                      "document");
        issues = validate(loaded(document));
        QVERIFY2(has(issues, Severity::Warning, "resource.subject.reference", "does not resolve within this document"),
                 qPrintable(dump(issues)));

        // The same version twice is still a duplicate.
        const QJsonObject twice = bundle({entry("http://example.org/fhir/Patient/test-patient", v1),
                                          entry("http://example.org/fhir/Patient/test-patient", v1)});
        issues = validate(loaded(twice));
        QVERIFY(has(issues, Severity::Error, "fullUrl", "fullUrl is also used by entry 0"));
        QVERIFY(has(issues, Severity::Warning, "resource.id", "appears more than once"));

        // Observation.issued stays an instant.
        QJsonObject observed = observation(kTestPatient, "2024-01-01", 7);
        observed.insert("issued", "2024-01-01");
        QVERIFY(has(validate(loaded(bundle({entry(kTestPatient, testPatient()), entry(uuid(222), observed)}))),
                    Severity::Error, "resource.issued", "not a valid FHIR instant"));
    }

    void svgExportFollowsTheChoiceNotTheName()
    {
        QTemporaryDir dir;
        write(dir.filePath("b.json"), QJsonDocument(bundle({entry(kTestPatient, testPatient()),
                                                            entry(uuid(180), observation(kTestPatient, "2024-01-01", 7.2))}))
                                          .toJson());
        Backend backend;
        QVERIFY(backend.openPath(dir.filePath("b.json")));
        QVERIFY(backend.exportTo("timeline-svg", dir.filePath("timeline")));
        QVERIFY(backend.exportTo("timeline-pdf", dir.filePath("printout")));
        QFile svg(dir.filePath("timeline")), pdf(dir.filePath("printout"));
        QVERIFY(svg.open(QIODevice::ReadOnly) && svg.readAll().contains("<svg"));
        QVERIFY(pdf.open(QIODevice::ReadOnly) && pdf.read(4) == "%PDF");
    }

    void opensInTheBackground()
    {
        QTemporaryDir dir;
        write(dir.filePath("first.json"), QJsonDocument(bundle({entry(kTestPatient, testPatient())})).toJson());
        write(dir.filePath("second.json"), QJsonDocument(bundle({entry(kTestPatient, testPatient()),
                                                                 entry(uuid(210), observation(kTestPatient, "2024-01-01", 7))}))
                                               .toJson());
        Backend backend;
        backend.openInBackground(dir.filePath("first.json"));
        QVERIFY(backend.loading());
        // A second open replaces the first; only its result lands.
        backend.openInBackground(dir.filePath("second.json"));
        // Whoever waits for loading to end finds the file already there.
        int resourcesWhenDone = -1;
        connect(&backend, &Backend::loadingChanged, this, [&] {
            if (!backend.loading() && resourcesWhenDone < 0)
                resourcesWhenDone = backend.resourceCount();
        });
        QTRY_VERIFY(!backend.loading());
        QCOMPARE(resourcesWhenDone, 2);
        QCOMPARE(backend.fileName(), QString("second.json"));
        QCOMPARE(backend.resourceCount(), 2);
        backend.openInBackground(dir.filePath("missing.json"));
        QTRY_VERIFY(!backend.loading());
        QVERIFY(!backend.loadError().isEmpty());
        QCOMPARE(backend.fileName(), QString("second.json"));
    }

    void exportsInTheBackground()
    {
        QTemporaryDir dir;
        write(dir.filePath("b.json"), QJsonDocument(bundle({entry(kTestPatient, testPatient()),
                                                            entry(uuid(230), observation(kTestPatient, "2024-01-01", 7.2)),
                                                            entry(uuid(231), observation(kTestPatient, "2024-02-01", 7.0))}))
                                          .toJson());
        Backend backend;
        QVERIFY(backend.openPath(dir.filePath("b.json")));
        backend.setRedact(true);
        backend.exportInBackground("timeline-pdf", dir.filePath("t.pdf"));
        backend.exportInBackground("timeline-csv", dir.filePath("t.csv"));
        QVERIFY(backend.exporting());
        // Changing the view meanwhile doesn't change what is being written.
        backend.setRedact(false);
        QTRY_VERIFY(!backend.exporting());
        QFile pdf(dir.filePath("t.pdf")), csv(dir.filePath("t.csv"));
        QVERIFY(pdf.open(QIODevice::ReadOnly) && pdf.read(4) == "%PDF");
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QByteArray text = csv.readAll();
        QVERIFY2(text.contains("Patient 1") && !text.contains("Test Patient"), text.constData());
        backend.exportInBackground("timeline-csv", dir.filePath("b.json"));
        QVERIFY(!backend.exporting());
    }

    void incomingReferencesAreIndexed()
    {
        QTemporaryDir dir;
        write(dir.filePath("b.json"), QJsonDocument(bundle({entry(kTestPatient, testPatient()),
                                                            entry(uuid(190), observation(kTestPatient, "2024-01-01", 7.2)),
                                                            entry(uuid(191), observation("Patient/test-patient", "2024-02-01", 7.0))}))
                                          .toJson());
        Backend backend;
        QVERIFY(backend.openPath(dir.filePath("b.json")));
        int incoming = 0, outgoing = 0;
        for (const auto &item : backend.references(1))
            (item.toMap().value("incoming").toBool() ? incoming : outgoing) += 1;
        QCOMPARE(incoming, 2);
        QCOMPARE(outgoing, 0);
        const QVariantList fromObservation = backend.references(2);
        QCOMPARE(fromObservation.size(), 1);
        QCOMPARE(fromObservation.first().toMap().value("target").toInt(), 1);
    }

    // The real window, offscreen, driven from the keyboard as a person would.
    void windowDrivesFromTheKeyboard()
    {
        QTemporaryDir dir;
        write(dir.filePath("language"), "en\n");
        QQuickStyle::setStyle("Material");
        Translator translator(dir.filePath("language"));
        Theme theme(dir.filePath("no-omarchy"));
        Backend backend;
        QVERIFY(backend.openPath(QFINDTESTDATA("../examples/synthetic-bundle.json")));
        qmlRegisterType<TrendChart>("Fhir", 1, 0, "TrendChart");
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto eval = [&](const QString &js) { return QQmlExpression(qmlContext(window), window, js).evaluate(); };
        auto key = [&](int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
            QTest::keyClick(window, Qt::Key(k), m);
            QTest::qWait(30);
        };
        auto letter = [&](char c) {
            QTest::keyClick(window, c);
            QTest::qWait(30);
        };

        // The first event is selected, and J and K move between events.
        QCOMPARE(eval("timelinePane.currentRow").toInt(), 1);
        letter('j'); letter('j'); letter('j');
        QCOMPARE(eval("timelinePane.current.summary").toString(), QString("Hemoglobin A1c 7.9 %"));
        QVERIFY(eval("timelinePane.detail.hasTrend").toBool());
        letter('k');

        // Enter opens the resource, and its references can be followed and walked back.
        key(Qt::Key_Return);
        QCOMPARE(eval("view").toInt(), 1);
        QCOMPARE(eval("resourcesPane.current.label").toString(), QString("Encounter/000000000101"));
        key(Qt::Key_Return);
        QCOMPARE(eval("resourcesPane.current.label").toString(), QString("Patient/test-patient"));
        key(Qt::Key_Backspace);
        QCOMPARE(eval("resourcesPane.current.label").toString(), QString("Encounter/000000000101"));
        key(Qt::Key_Backspace);
        QCOMPARE(eval("view").toInt(), 0);

        // An issue jumps to its resource with the path highlighted.
        key(Qt::Key_Tab); key(Qt::Key_Tab);
        QCOMPARE(eval("view").toInt(), 2);
        QCOMPARE(eval("issuesPane.rows.count").toInt(), 4);
        letter('j');
        key(Qt::Key_Return);
        QCOMPARE(eval("resourcesPane.highlightPath").toString(), QString("resource.subject.reference"));
        QVERIFY(eval("resourcesPane.shown.end > resourcesPane.shown.start").toBool());

        // P redacts what the window shows.
        letter('p');
        QVERIFY(backend.redact());
        QVERIFY(!eval("resourcesPane.shown.text").toString().contains("Sampletown"));
        letter('p');

        // Search takes text, Q included, without quitting.
        letter('1');
        letter('/');
        QVERIFY(eval("typing").toBool());
        for (char c : QByteArray("a1c q"))
            letter(c);
        QTRY_COMPARE(backend.searchText(), QString("a1c q"));
        key(Qt::Key_Escape);
        QCOMPARE(backend.searchText(), QString());
        QVERIFY(!eval("typing").toBool());

        // F, ?, and Ctrl+E open their overlays, and Esc closes each.
        for (const auto &[open, visible] : {std::pair{0, "filterPopup.visible"}, std::pair{1, "shortcutsOverlay.visible"},
                                           std::pair{2, "exportMenu.visible"}}) {
            if (open == 0) letter('f');
            else if (open == 1) letter('?');
            else key(Qt::Key_E, Qt::ControlModifier);
            QTRY_VERIFY2(eval(visible).toBool(), visible);
            key(Qt::Key_Escape);
            QTRY_VERIFY2(!eval(visible).toBool(), visible);
        }

        // Space hides the detail pane; Enter on a type heading folds it.
        key(Qt::Key_Space);
        QVERIFY(!eval("detailVisible").toBool());
        key(Qt::Key_Space);
        key(Qt::Key_Tab);
        key(Qt::Key_Home);
        const int rows = eval("resourcesPane.rows.count").toInt();
        key(Qt::Key_Return);
        QCOMPARE(eval("resourcesPane.rows.count").toInt(), rows - 1);
        key(Qt::Key_Return);
        QCOMPARE(eval("resourcesPane.rows.count").toInt(), rows);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void shippedCatalogsParse()
    {
        // Every translation covers the same strings as Danish, the most
        // complete, and keeps each placeholder its English source has.
        auto load = [](const QString &code) {
            QFile catalog(QString(":/i18n/%1.json").arg(code));
            if (!catalog.open(QIODevice::ReadOnly))
                return QJsonObject();
            return QJsonDocument::fromJson(catalog.readAll()).object();
        };
        QVERIFY(!load("en").isEmpty());
        const QJsonObject reference = load("da");
        QVERIFY(reference.size() > 100);
        static const QRegularExpression placeholder("%[0-9n]");
        auto placeholders = [&](const QString &text) {
            QStringList found;
            for (auto it = placeholder.globalMatch(text); it.hasNext();)
                found << it.next().captured();
            found.sort();
            return found;
        };
        for (const char *code : {"de", "es-MX", "fr", "pt-BR"}) {
            const QJsonObject catalog = load(code);
            const QStringList keys = catalog.keys(), expected = reference.keys();
            QCOMPARE(QSet<QString>(keys.begin(), keys.end()), QSet<QString>(expected.begin(), expected.end()));
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                const QStringList forms = it.value().isObject() ? QStringList{it.value().toObject().value("one").toString(),
                                                                                it.value().toObject().value("other").toString()}
                                                                 : QStringList{it.value().toString()};
                for (const auto &form : forms)
                    QVERIFY2(placeholders(form) == placeholders(it.key()) ||
                                 (it.value().isObject() && placeholders(form).contains("%n")),
                             qPrintable(QString("%1: %2").arg(code, it.key())));
            }
        }
    }
};

QTEST_MAIN(FhirTests)
#include "fhir_tests.moc"
