---
name: fhir
description: Validate FHIR R4 JSON files (resources, Bundles, NDJSON bulk exports) with structural checks and read them as patient timelines, using the fhir command line tool. Use when asked to check, summarize, or list events from a FHIR file.
---

# fhir

`fhir` reads FHIR R4 JSON: one resource, a Bundle, or NDJSON bulk export. It
only ever reads the file. Every command works without a display.

- `fhir check <file> --json` validates. It exits 1 when there are errors;
  warnings fail only with `--strict`. Each issue has `severity` (error,
  warning, info), `location` (`Bundle.entry[3].resource.code`), `message`, and
  the offending `value`.
- `fhir summary <file> --json` gives resource counts, patients, and the date
  range of dated events.
- `fhir timeline <file> --json` (or `--csv`) lists dated events by patient in
  date order. Narrow it with `--patient <id>`, `--type Observation,Condition`,
  `--from 2024-01-01`, `--to 2024-12-31`, and `--search <text>`.
- Add `--redact` to any command to mask names, identifiers, addresses,
  telecom, and birth dates before the output goes anywhere else.
- `fhir open <file>` opens the window for a person to look at.

The checks are structural only: valid JSON, known resource types, required
elements and required code sets for Patient, Encounter, Observation,
Condition, MedicationRequest, Procedure, AllergyIntolerance, Immunization,
and DiagnosticReport, primitive formats, and references that resolve within
the file. They do not check profiles, terminology, or FHIRPath invariants.
Never describe a clean `fhir check` as conformance, and never use the output
for clinical decisions.

When the file may hold real patient data, pass `--redact` and don't copy
identifying details into chats, commits, or issues.
