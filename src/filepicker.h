#pragma once

#include <QObject>
#include <QUrl>

// Open and save dialogs, behind an interface so tests can inject a fake.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openFhir() = 0;
    // format is the file extension of what will be written: json, csv, pdf, or svg.
    virtual void saveFile(const QUrl &suggestedUrl, const QString &format) = 0;

signals:
    void openSelected(const QUrl &url);
    void saveSelected(const QUrl &url);
    void failed(const QString &message);
};
