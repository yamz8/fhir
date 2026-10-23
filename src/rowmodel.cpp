#include "rowmodel.h"

RowModel::RowModel(const QStringList &roles, Cell cell, QObject *parent)
    : QAbstractListModel(parent), m_roles(roles), m_cell(std::move(cell)) {}

void RowModel::resetRows(int count) {
    beginResetModel();
    m_count = count;
    ++m_revision;
    endResetModel();
    emit reset();
}

QVariant RowModel::data(const QModelIndex &index, int role) const {
    const int column = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= m_count || column < 0 || column >= m_roles.size())
        return {};
    return m_cell(index.row(), m_roles[column]);
}

QHash<int, QByteArray> RowModel::roleNames() const {
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_roles.size(); ++i)
        names.insert(Qt::UserRole + i, m_roles[i].toUtf8());
    return names;
}

QVariantMap RowModel::get(int row) const {
    QVariantMap map;
    if (row < 0 || row >= m_count)
        return map;
    for (const auto &role : m_roles)
        map.insert(role, m_cell(row, role));
    return map;
}

QVariant RowModel::value(int row, const QString &role) const {
    return row >= 0 && row < m_count ? m_cell(row, role) : QVariant();
}

int RowModel::find(const QString &role, const QVariant &value, int from, int step) const {
    if (step == 0)
        return -1;
    for (int row = from; row >= 0 && row < m_count; row += step)
        if (m_cell(row, role) == value)
            return row;
    return -1;
}

QVariantList RowModel::all() const {
    QVariantList rows;
    for (int row = 0; row < m_count; ++row)
        rows << get(row);
    return rows;
}
