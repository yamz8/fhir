#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

#include "report.h"
#include "timeline.h"
#include "validator.h"

using namespace fhir;

namespace {

// "1 error", "2 errors".
QString count(int n, const QString &noun) {
    return QString("%1 %2%3").arg(n).arg(noun, n == 1 || noun == "info" ? "" : "s");
}
void print(FILE *stream, const QString &text) {
    fprintf(stream, "%s\n", qPrintable(text));
}
void print(const QJsonObject &object) {
    fprintf(stdout, "%s", QJsonDocument(object).toJson().constData());
}
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description, bool file = true) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        if (file)
            parser.addPositionalArgument("file", "FHIR R4 JSON resource, Bundle, or NDJSON file", "<file>");
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    void redact() { parser.addOption({"redact", "Mask names, identifiers, addresses, telecom, and birth dates"}); }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    bool load(File &file) const {
        if (argument(1).isEmpty()) {
            print(stderr, "Name a FHIR JSON file.");
            return false;
        }
        if (!file.load(argument(1))) {
            print(stderr, argument(1) + ": " + file.error);
            return false;
        }
        return true;
    }
};

int check(const QStringList &arguments) {
    Command command("check", "Validate a FHIR R4 file with structural checks only. Exits 1 when there are errors.");
    command.json();
    command.redact();
    command.parser.addOption({"strict", "Fail on warnings too"});
    command.parser.process(arguments);
    File file;
    if (!command.load(file))
        return 1;
    const QList<Issue> issues = validate(file);
    const IssueCounts counts = countIssues(issues);
    const bool redacted = command.parser.isSet("redact");
    const bool failed = counts.errors > 0 || (command.parser.isSet("strict") && counts.warnings > 0);
    if (command.parser.isSet("json")) {
        QJsonObject report = issuesJson(file, issues, redacted);
        report.insert("ok", !failed);
        report.insert("strict", command.parser.isSet("strict"));
        print(report);
        return failed ? 1 : 0;
    }
    for (const auto &issue : issues)
        print(issue.severity == Severity::Error ? stderr : stdout,
              QString("%1: %2: %3: %4")
                  .arg(command.argument(1), severityName(issue.severity), file.location(issue),
                       issueText(issue, redacted)));
    print(stdout, QString("%1, %2, %3, %4 (%5)")
                      .arg(count(file.entryCount(), "resource"), count(counts.errors, "error"),
                           count(counts.warnings, "warning"), count(counts.infos, "info"), kScope));
    return failed ? 1 : 0;
}

int summary(const QStringList &arguments) {
    Command command("summary", "Print resource counts, patients, and the date range.");
    command.json();
    command.redact();
    command.parser.process(arguments);
    File file;
    if (!command.load(file))
        return 1;
    const QList<Issue> issues = validate(file);
    const Timeline timeline = buildTimeline(file);
    const QJsonObject result = summaryJson(file, issues, timeline, command.parser.isSet("redact"));
    if (command.parser.isSet("json")) {
        print(result);
        return 0;
    }
    print(stdout, QString("%1 (%2), %3, %4")
                      .arg(result.value("file").toString(), result.value("kind").toString(),
                           count(result.value("resources").toInt(), "resource"),
                           count(result.value("events").toInt(), "event")));
    const QJsonObject counts = result.value("counts").toObject();
    for (auto it = counts.begin(); it != counts.end(); ++it)
        print(stdout, QString("  %1 %2").arg(it.value().toInt(), 5).arg(it.key()));
    for (const auto &item : result.value("patients").toArray()) {
        const QJsonObject patient = item.toObject();
        print(stdout, QString("%1: %2, %3")
                          .arg(patient.value("reference").toString(), patient.value("name").toString(),
                               count(patient.value("events").toInt(), "event")));
    }
    const QJsonObject range = result.value("dateRange").toObject();
    if (!range.value("start").isNull())
        print(stdout, QString("Dates: %1 to %2").arg(range.value("start").toString(), range.value("end").toString()));
    const QJsonObject found = result.value("issues").toObject();
    print(stdout, QString("Issues: %1, %2, %3 (%4)")
                      .arg(count(found.value("errors").toInt(), "error"), count(found.value("warnings").toInt(), "warning"),
                           count(found.value("info").toInt(), "info"), kScope));
    return 0;
}

int timeline(const QStringList &arguments) {
    Command command("timeline", "Print the dated events, grouped by patient, in date order.");
    command.json();
    command.redact();
    command.parser.addOption({"csv", "Print the timeline as CSV"});
    command.parser.addOption({"patient", "Only this patient (id or reference)", "id"});
    command.parser.addOption({"type", "Only these resource types (comma separated)", "types"});
    command.parser.addOption({"from", "Only events on or after this date", "YYYY-MM-DD"});
    command.parser.addOption({"to", "Only events on or before this date", "YYYY-MM-DD"});
    command.parser.addOption({"search", "Only events matching this text", "text"});
    command.parser.process(arguments);
    File file;
    if (!command.load(file))
        return 1;
    Filter filter;
    filter.patient = command.parser.value("patient");
    filter.text = command.parser.value("search");
    for (const auto &type : command.parser.value("type").split(',', Qt::SkipEmptyParts))
        filter.types.insert(type.trimmed());
    for (const char *name : {"from", "to"}) {
        if (!command.parser.isSet(name))
            continue;
        const QDate date = QDate::fromString(command.parser.value(name), Qt::ISODate);
        if (!date.isValid())
            return fail(QString("--%1 needs a date like 2024-01-31.").arg(name));
        (QString(name) == "from" ? filter.from : filter.to) = date;
    }
    const Timeline built = buildTimeline(file);
    const bool redacted = command.parser.isSet("redact");
    if (!filter.patient.isEmpty()) {
        bool known = false;
        for (const auto &event : built.events)
            known = known || filter.matches(file, event);
        if (!known)
            return fail("No events for patient " + filter.patient + ".");
    }
    if (command.parser.isSet("json")) {
        print(timelineJson(file, built, filter, redacted));
        return 0;
    }
    if (command.parser.isSet("csv")) {
        fputs(timelineCsv(file, built, filter, redacted).constData(), stdout);
        return 0;
    }
    const QJsonObject result = timelineJson(file, built, filter, redacted);
    for (const auto &item : result.value("patients").toArray()) {
        const QJsonObject group = item.toObject();
        print(stdout, group.value("patient").toString());
        for (const auto &value : group.value("events").toArray()) {
            const QJsonObject event = value.toObject();
            print(stdout, QString("  %1  %2  %3")
                              .arg(event.value("date").toString(), -25)
                              .arg(event.value("type").toString(), -18)
                              .arg(event.value("summary").toString()));
        }
    }
    return 0;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.", false);
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/fhir and link into ~/.claude/skills",
                                         "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use fhir skill to print the skill, or fhir skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/fhir");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/fhir");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/fhir", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    const QString command = arguments.value(1);
    if (!command.isEmpty() && command != "help")
        print(stderr, "Unknown command " + command + ".\n");
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: fhir <command> [options]\n\n" + cliSummary());
    return command.isEmpty() || command == "help" ? 0 : 1;
}

} // namespace

QString cliSummary() {
    return "Viewer:\n"
           "  open [file]                 Open the window, on a file if given\n\n"
           "Commands that need no display (fhir help <command> for options):\n"
           "  check <file>                Validate (structural checks only); exits 1 on errors\n"
           "  summary <file>              Resource counts, patients, and date range\n"
           "  timeline <file>             Dated events by patient, as text, --json, or --csv\n"
           "  skill [install]             Print the skill for coding agents, or install it\n\n"
           "fhir is not a certified validator and is not for clinical decision-making.";
}

bool isCliCommand(const QString &word) {
    return QStringList{"check", "summary", "timeline", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "check"      ? check(arguments)
           : command == "summary"  ? summary(arguments)
           : command == "timeline" ? timeline(arguments)
           : command == "skill"    ? skill(arguments)
                                   : help(arguments);
}
