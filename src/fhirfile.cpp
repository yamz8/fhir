#include "fhirfile.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace fhir {

namespace {
QString tr(const char *text) {
    return QCoreApplication::translate("File", text);
}
} // namespace

QString severityName(Severity severity) {
    return severity == Severity::Error ? "error" : severity == Severity::Warning ? "warning" : "info";
}

bool isKnownResourceType(const QString &type) {
    // Every resource type in FHIR R4 (4.0.1).
    static const QSet<QString> types = {
        "Account", "ActivityDefinition", "AdverseEvent", "AllergyIntolerance", "Appointment",
        "AppointmentResponse", "AuditEvent", "Basic", "Binary", "BiologicallyDerivedProduct",
        "BodyStructure", "Bundle", "CapabilityStatement", "CarePlan", "CareTeam", "CatalogEntry",
        "ChargeItem", "ChargeItemDefinition", "Claim", "ClaimResponse", "ClinicalImpression",
        "CodeSystem", "Communication", "CommunicationRequest", "CompartmentDefinition", "Composition",
        "ConceptMap", "Condition", "Consent", "Contract", "Coverage", "CoverageEligibilityRequest",
        "CoverageEligibilityResponse", "DetectedIssue", "Device", "DeviceDefinition", "DeviceMetric",
        "DeviceRequest", "DeviceUseStatement", "DiagnosticReport", "DocumentManifest",
        "DocumentReference", "EffectEvidenceSynthesis", "Encounter", "Endpoint", "EnrollmentRequest",
        "EnrollmentResponse", "EpisodeOfCare", "EventDefinition", "Evidence", "EvidenceVariable",
        "ExampleScenario", "ExplanationOfBenefit", "FamilyMemberHistory", "Flag", "Goal",
        "GraphDefinition", "Group", "GuidanceResponse", "HealthcareService", "ImagingStudy",
        "Immunization", "ImmunizationEvaluation", "ImmunizationRecommendation", "ImplementationGuide",
        "InsurancePlan", "Invoice", "Library", "Linkage", "List", "Location", "Measure",
        "MeasureReport", "Media", "Medication", "MedicationAdministration", "MedicationDispense",
        "MedicationKnowledge", "MedicationRequest", "MedicationStatement", "MedicinalProduct",
        "MedicinalProductAuthorization", "MedicinalProductContraindication",
        "MedicinalProductIndication", "MedicinalProductIngredient", "MedicinalProductInteraction",
        "MedicinalProductManufactured", "MedicinalProductPackaged", "MedicinalProductPharmaceutical",
        "MedicinalProductUndesirableEffect", "MessageDefinition", "MessageHeader", "MolecularSequence",
        "NamingSystem", "NutritionOrder", "Observation", "ObservationDefinition",
        "OperationDefinition", "OperationOutcome", "Organization", "OrganizationAffiliation",
        "Parameters", "Patient", "PaymentNotice", "PaymentReconciliation", "Person", "PlanDefinition",
        "Practitioner", "PractitionerRole", "Procedure", "Provenance", "Questionnaire",
        "QuestionnaireResponse", "RelatedPerson", "RequestGroup", "ResearchDefinition",
        "ResearchElementDefinition", "ResearchStudy", "ResearchSubject", "RiskAssessment",
        "RiskEvidenceSynthesis", "Schedule", "SearchParameter", "ServiceRequest", "Slot", "Specimen",
        "SpecimenDefinition", "StructureDefinition", "StructureMap", "Subscription", "Substance",
        "SubstanceNucleicAcid", "SubstancePolymer", "SubstanceProtein",
        "SubstanceReferenceInformation", "SubstanceSourceMaterial", "SubstanceSpecification",
        "SupplyDelivery", "SupplyRequest", "Task", "TerminologyCapabilities", "TestReport",
        "TestScript", "ValueSet", "VerificationResult", "VisionPrescription",
    };
    return types.contains(type);
}

QStringList coreResourceTypes() {
    return {"Patient", "Encounter", "Observation", "Condition", "MedicationRequest", "Procedure",
            "AllergyIntolerance", "Immunization", "DiagnosticReport"};
}

QString childPath(const QString &path, const QString &key) {
    return path.isEmpty() ? key : path + "." + key;
}

QString indexPath(const QString &path, int index) {
    return path + "[" + QString::number(index) + "]";
}

QString conceptText(const QJsonValue &value) {
    const QJsonObject cc = value.toObject();
    if (!cc.value("text").toString().isEmpty())
        return cc.value("text").toString();
    const QJsonArray codings = cc.value("coding").toArray();
    for (const auto &coding : codings)
        if (!coding.toObject().value("display").toString().isEmpty())
            return coding.toObject().value("display").toString();
    for (const auto &coding : codings)
        if (!coding.toObject().value("code").toString().isEmpty())
            return coding.toObject().value("code").toString();
    return {};
}

QStringList conceptCodes(const QJsonValue &value) {
    QStringList loinc, other;
    for (const auto &item : value.toObject().value("coding").toArray()) {
        const QJsonObject coding = item.toObject();
        const QString code = coding.value("code").toString();
        if (code.isEmpty())
            continue;
        const QString system = coding.value("system").toString();
        (system == "http://loinc.org" ? loinc : other) << system + "|" + code;
    }
    return loinc + other;
}

QString patientName(const QJsonObject &patient) {
    const QJsonArray names = patient.value("name").toArray();
    // The official name when there is one, else the first.
    QJsonObject chosen = names.isEmpty() ? QJsonObject() : names.first().toObject();
    for (const auto &name : names)
        if (name.toObject().value("use").toString() == "official") {
            chosen = name.toObject();
            break;
        }
    if (!chosen.value("text").toString().isEmpty())
        return chosen.value("text").toString();
    QStringList parts;
    for (const auto &given : chosen.value("given").toArray())
        parts << given.toString();
    if (!chosen.value("family").toString().isEmpty())
        parts << chosen.value("family").toString();
    return parts.join(' ').simplified();
}

void File::reset() {
    kind = Kind::Empty;
    error.clear();
    resources.clear();
    parseIssues.clear();
    m_byUrl.clear();
    m_byTypeId.clear();
}

bool File::load(const QString &filePath) {
    reset();
    path = filePath;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    loadData(file.readAll(), QFileInfo(filePath).fileName());
    return true;
}

void File::addParseError(const QJsonParseError &parseError, const QByteArray &data, int line, int base) {
    // Point at the line and column for people; QJsonParseError only gives an offset.
    const QByteArray before = data.left(parseError.offset);
    const int row = before.count('\n');
    const int column = parseError.offset - (before.lastIndexOf('\n') + 1) + 1;
    Issue issue;
    issue.message = tr("Invalid JSON at line %1, column %2: %3")
                        .arg(line ? line : base + row + 1)
                        .arg(column)
                        .arg(parseError.errorString());
    parseIssues << issue;
}

void File::loadData(const QByteArray &data, const QString &name) {
    reset();
    if (path.isEmpty())
        path = name;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    const bool ndjsonName = name.endsWith(".ndjson", Qt::CaseInsensitive);
    QList<QByteArray> lines = data.split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    int filled = 0;
    for (const auto &line : lines)
        filled += !line.trimmed().isEmpty();

    // Bulk export puts one resource per line. A file is NDJSON by its name, or
    // when it isn't one JSON document but its first line is.
    const bool firstLineParses =
        !lines.isEmpty() && QJsonDocument::fromJson(lines.first()).isObject();
    if (ndjsonName || (parseError.error != QJsonParseError::NoError && filled > 1 && firstLineParses)) {
        kind = Kind::Ndjson;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].trimmed().isEmpty())
                continue;
            QJsonParseError lineError;
            const QJsonDocument lineDocument = QJsonDocument::fromJson(lines[i], &lineError);
            if (lineError.error != QJsonParseError::NoError) {
                addParseError(lineError, lines[i], i + 1, 0);
                continue;
            }
            if (!lineDocument.isObject()) {
                parseIssues << Issue{Severity::Error, -1, QString(),
                                     tr("Line %1 is not a JSON object").arg(i + 1), QString()};
                continue;
            }
            Resource resource;
            resource.json = resource.shown = lineDocument.object();
            resource.type = resource.json.value("resourceType").toString();
            resource.id = resource.json.value("id").toString();
            resource.line = i + 1;
            resources << resource;
        }
        buildIndex();
        return;
    }

    if (parseError.error != QJsonParseError::NoError) {
        addParseError(parseError, data, 0, 0);
        return;
    }
    if (!document.isObject()) {
        parseIssues << Issue{Severity::Error, -1, QString(),
                             tr("The file holds a JSON array; a FHIR resource is one JSON object"), QString()};
        return;
    }

    const QJsonObject root = document.object();
    if (root.value("resourceType").toString() != "Bundle") {
        kind = Kind::Single;
        Resource resource;
        resource.json = resource.shown = root;
        resource.type = root.value("resourceType").toString();
        resource.id = root.value("id").toString();
        resources << resource;
        buildIndex();
        return;
    }

    kind = Kind::Bundle;
    Resource bundle;
    bundle.json = root;
    // The Bundle's own pane leaves out the entries, which are listed by type.
    bundle.shown = root;
    bundle.shown.remove("entry");
    bundle.type = "Bundle";
    bundle.id = root.value("id").toString();
    bundle.container = true;
    resources << bundle;

    const QJsonArray entries = root.value("entry").toArray();
    for (int i = 0; i < entries.size(); ++i) {
        const QJsonObject entry = entries[i].toObject();
        Resource resource;
        resource.shown = entry;
        resource.entry = i;
        resource.fullUrl = entry.value("fullUrl").toString();
        if (entry.value("resource").isObject()) {
            resource.json = entry.value("resource").toObject();
            resource.resourcePath = "resource";
            resource.type = resource.json.value("resourceType").toString();
            resource.id = resource.json.value("id").toString();
        }
        resources << resource;
    }
    buildIndex();
}

void File::buildIndex() {
    static const QRegularExpression restful(R"(([A-Z][A-Za-z]+)/([A-Za-z0-9\-\.]{1,64})(/_history/[^/]+)?$)");
    for (int i = 0; i < resources.size(); ++i) {
        const Resource &resource = resources[i];
        if (resource.container || resource.type.isEmpty())
            continue;
        if (!resource.fullUrl.isEmpty() && !m_byUrl.contains(resource.fullUrl))
            m_byUrl.insert(resource.fullUrl, i);
        if (!resource.id.isEmpty() && !m_byTypeId.contains(resource.type + "/" + resource.id))
            m_byTypeId.insert(resource.type + "/" + resource.id, i);
        // A RESTful fullUrl names the resource too, even when its id differs.
        const auto match = restful.match(resource.fullUrl);
        if (match.hasMatch() && !m_byTypeId.contains(match.captured(1) + "/" + match.captured(2)))
            m_byTypeId.insert(match.captured(1) + "/" + match.captured(2), i);
    }
}

int File::resolve(const QString &reference) const {
    if (reference.isEmpty() || reference.startsWith('#'))
        return -1;
    if (m_byUrl.contains(reference))
        return m_byUrl.value(reference);
    static const QRegularExpression restful(R"((?:^|/)([A-Z][A-Za-z]+)/([A-Za-z0-9\-\.]{1,64})(/_history/[^/]+)?$)");
    const auto match = restful.match(reference);
    if (match.hasMatch())
        return m_byTypeId.value(match.captured(1) + "/" + match.captured(2), -1);
    return -1;
}

QString File::patientReference(int index) const {
    if (index < 0 || index >= resources.size())
        return {};
    const QJsonObject json = resources[index].json;
    for (const char *key : {"subject", "patient"}) {
        const QString reference = json.value(key).toObject().value("reference").toString();
        if (!reference.isEmpty())
            return reference;
    }
    return {};
}

int File::patientOf(int index) const {
    if (index < 0 || index >= resources.size())
        return -1;
    if (resources[index].type == "Patient")
        return index;
    const int target = resolve(patientReference(index));
    return target >= 0 && resources[target].type == "Patient" ? target : -1;
}

QString File::location(const Issue &issue) const {
    if (issue.resource < 0 || issue.resource >= resources.size())
        return issue.path.isEmpty() ? QString("(file)") : issue.path;
    const Resource &resource = resources[issue.resource];
    QString base;
    if (resource.container)
        base = "Bundle";
    else if (resource.entry >= 0)
        base = indexPath("Bundle.entry", resource.entry);
    else
        base = resource.type.isEmpty() ? QString("(resource)") : resource.type;
    const QString full = issue.path.isEmpty() ? base
                         : issue.path.startsWith('[') ? base + issue.path
                                                      : base + "." + issue.path;
    return resource.line ? QString("line %1: %2").arg(resource.line).arg(full) : full;
}

QString File::label(int index) const {
    if (index < 0 || index >= resources.size())
        return {};
    const Resource &resource = resources[index];
    if (resource.type.isEmpty())
        return resource.entry >= 0 ? indexPath("entry", resource.entry) : QString("(no resourceType)");
    return resource.id.isEmpty() ? resource.type : resource.type + "/" + resource.id;
}

int File::entryCount() const {
    int count = 0;
    for (const auto &resource : resources)
        count += !resource.container;
    return count;
}

} // namespace fhir
