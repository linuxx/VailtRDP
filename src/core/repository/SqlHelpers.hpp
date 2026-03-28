#pragma once

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <optional>

namespace vaultrdp::core::repository::sql {

inline QVariant nullableString(const std::optional<QString>& value) {
  if (value.has_value() && !value->trimmed().isEmpty()) {
    return value.value();
  }
  return QVariant();
}

inline QVariant nullableString(const QString& value) {
  if (!value.trimmed().isEmpty()) {
    return value;
  }
  return QVariant();
}

inline std::optional<QString> optionalString(const QVariant& value) {
  if (value.isNull()) {
    return std::nullopt;
  }
  const QString text = value.toString();
  if (text.trimmed().isEmpty()) {
    return std::nullopt;
  }
  return text;
}

inline bool execOrLog(QSqlQuery& query, const char* context) {
  if (query.exec()) {
    return true;
  }
  qCritical() << context << query.lastError().text();
  return false;
}

}  // namespace vaultrdp::core::repository::sql

