QT += core gui qml quick quickcontrols2 testlib dbus svg
CONFIG += c++17 testcase
TARGET = fhir_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/cli.h \
    ../src/fhirfile.h \
    ../src/filepicker.h \
    ../src/jsonview.h \
    ../src/redact.h \
    ../src/report.h \
    ../src/theme.h \
    ../src/timeline.h \
    ../src/translator.h \
    ../src/trendchart.h \
    ../src/validator.h

SOURCES += \
    fhir_tests.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/fhirfile.cpp \
    ../src/jsonview.cpp \
    ../src/redact.cpp \
    ../src/report.cpp \
    ../src/theme.cpp \
    ../src/timeline.cpp \
    ../src/translator.cpp \
    ../src/trendchart.cpp \
    ../src/validator.cpp

RESOURCES += ../src/resources.qrc
