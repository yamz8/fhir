#pragma once

#include <QByteArray>
#include <QJsonObject>

#include "fhirfile.h"
#include "timeline.h"
#include "trendchart.h"

namespace fhir {

// The message with its value, or with the value hidden when it is identifying.
QString issueText(const Issue &issue, bool redacted);

QJsonObject issuesJson(const File &file, const QList<Issue> &issues, bool redacted);
QByteArray issuesCsv(const File &file, const QList<Issue> &issues, bool redacted);

QJsonObject summaryJson(const File &file, const QList<Issue> &issues, const Timeline &timeline, bool redacted);

QJsonObject timelineJson(const File &file, const Timeline &timeline, const Filter &filter, bool redacted);
QByteArray timelineCsv(const File &file, const Timeline &timeline, const Filter &filter, bool redacted);

TrendSeries trendSeries(const Timeline &timeline, const QList<int> &events);

// A printable timeline: PDF pages, or one tall SVG, picked by the suffix.
bool exportTimelineDocument(const QString &path, const File &file, const Timeline &timeline, const Filter &filter,
                            bool redacted, const QLocale &locale, QString *error);

// Writes beside the target and renames over it, so a failed export never
// leaves half a file behind.
bool saveAtomically(const QString &path, const QByteArray &data, QString *error);

} // namespace fhir
