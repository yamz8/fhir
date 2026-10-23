#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <functional>

// A list for QML whose cells are worked out only when asked for, so a file
// with tens of thousands of rows costs only the rows on screen. QML reads a
// row with get(i) and finds one with find(role, value).
class RowModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY reset)
    Q_PROPERTY(int revision READ revision NOTIFY reset)

public:
    using Cell = std::function<QVariant(int row, const QString &role)>;

    RowModel(const QStringList &roles, Cell cell, QObject *parent = nullptr);

    // The rows changed; there are now `count` of them.
    void resetRows(int count);

    int count() const { return m_count; }
    int revision() const { return m_revision; }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_count; }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariant value(int row, const QString &role) const;
    // The first row from `from`, stepping by `step`, whose role equals value, or -1.
    Q_INVOKABLE int find(const QString &role, const QVariant &value, int from = 0, int step = 1) const;
    QVariantList all() const;

signals:
    void reset();

private:
    QStringList m_roles;
    Cell m_cell;
    int m_count = 0;
    int m_revision = 0;
};
