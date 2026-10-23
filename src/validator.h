#pragma once

#include "fhirfile.h"

namespace fhir {

// What every report says about its own reach.
inline constexpr const char *kScope = "structural checks only";

struct IssueCounts {
    int errors = 0, warnings = 0, infos = 0;
};

// Structural checks: valid JSON, known resourceType, required elements and
// required code sets for the core resource types, primitive formats, and
// references that resolve within the file. No profiles, terminology, or
// FHIRPath invariants, so a clean report is not a claim of conformance.
QList<Issue> validate(const File &file);
IssueCounts countIssues(const QList<Issue> &issues);

bool isValidDate(const QString &value);
bool isValidDateTime(const QString &value);
bool isValidInstant(const QString &value);
bool isValidId(const QString &value);
bool isValidCode(const QString &value);

} // namespace fhir
