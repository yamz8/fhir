#pragma once

#include <QStringList>

// Headless commands for scripts and agents: fhir check, summary, timeline, skill.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);
