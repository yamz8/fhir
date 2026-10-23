// fhir — open a FHIR R4 resource, Bundle, or NDJSON export, check its
// structure, and read it as a patient timeline.

#include <QFileInfo>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "translator.h"
#include "trendchart.h"

int main(int argc, char *argv[]) {
    // Bare fhir prints help, as a command line tool should; launchers say
    // fhir open. A file name alone opens the window too.
    const QString first = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const bool gui = first == "open" || (!first.isEmpty() && !isCliCommand(first) && QFileInfo(first).isFile());
    // Commands draw no window, so they must not need a display, even where
    // the desktop exports QT_QPA_PLATFORM=wayland.
    // The GTK platform theme would also try the accessibility bus and warn.
    if (!gui) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("fhir");
    app.setApplicationVersion("0.1.0");

    // Matches the window to fhir.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("fhir");
    app.setWindowIcon(QIcon::fromTheme("fhir"));

    if (!gui)
        return runCli(app.arguments());

    QCommandLineParser args;
    args.setApplicationDescription("Read FHIR R4 files as a patient timeline.\n\n" + cliSummary());
    args.addHelpOption();
    args.addVersionOption();
    args.addPositionalArgument("file", "FHIR R4 JSON resource, Bundle, or NDJSON file");
    args.addOption({"redact", "Start with identifying details masked"});
    args.addOption({"view", "Start on timeline, resources, or issues", "view"});
    args.addOption({"select", "Select this row of the view (0-based)", "row"});
    args.addOption({"screenshot", "Save a screenshot of the window and exit", "png"});
    QStringList arguments = app.arguments();
    if (arguments.value(1) == "open")
        arguments.removeAt(1);
    args.process(arguments);

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&picker);
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::relocalize);
    if (!args.positionalArguments().isEmpty())
        backend.openInBackground(args.positionalArguments().first());
    backend.setRedact(args.isSet("redact"));

    qmlRegisterType<TrendChart>("Fhir", 1, 0, "TrendChart");
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            fprintf(stderr, "%s\n", qPrintable(error.toString()));
    });
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject *window = engine.rootObjects().first();
    // The view, selection, and screenshot apply once the file has opened.
    auto ready = [&] {
        if (args.isSet("view"))
            QMetaObject::invokeMethod(window, "showView", Q_ARG(QVariant, args.value("view")));
        if (args.isSet("select"))
            QMetaObject::invokeMethod(window, "selectRow", Q_ARG(QVariant, args.value("select").toInt()));
        if (args.isSet("screenshot")) {
            QTimer::singleShot(1200, &app, [&] {
                auto quick = qobject_cast<QQuickWindow *>(window);
                const bool ok = quick && quick->grabWindow().save(args.value("screenshot"));
                app.exit(ok ? 0 : 1);
            });
        }
    };
    if (backend.loading())
        QObject::connect(&backend, &Backend::loadingChanged, &app, ready, Qt::SingleShotConnection);
    else
        ready();
    return app.exec();
}
