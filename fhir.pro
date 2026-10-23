QT += core gui qml quick quickcontrols2 dbus svg

CONFIG += c++17 release
TARGET = fhir
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/fhirfile.h \
    src/filepicker.h \
    src/jsonview.h \
    src/portalfilepicker.h \
    src/redact.h \
    src/report.h \
    src/theme.h \
    src/timeline.h \
    src/translator.h \
    src/trendchart.h \
    src/validator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/fhirfile.cpp \
    src/jsonview.cpp \
    src/portalfilepicker.cpp \
    src/redact.cpp \
    src/report.cpp \
    src/theme.cpp \
    src/timeline.cpp \
    src/translator.cpp \
    src/trendchart.cpp \
    src/validator.cpp

RESOURCES += src/resources.qrc
