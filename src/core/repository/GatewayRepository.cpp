#include "core/repository/GatewayRepository.hpp"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "core/DatabaseManager.hpp"
#include "core/repository/RowMappers.hpp"
#include "core/Uuid.hpp"
#include "core/repository/SqlHelpers.hpp"

namespace vaultrdp::core::repository {

GatewayRepository::GatewayRepository(DatabaseManager* databaseManager) : databaseManager_(databaseManager) {}

std::optional<vaultrdp::model::Gateway> GatewayRepository::createGateway(
    const QString& name, const QString& host, int port,
    vaultrdp::model::GatewayCredentialMode credentialMode,
    const std::optional<QString>& credentialId,
    const std::optional<QString>& folderId,
    bool allowAnyFolder) const {
  if (name.trimmed().isEmpty() || host.trimmed().isEmpty() || port <= 0 || port > 65535) {
    return std::nullopt;
  }

  vaultrdp::model::Gateway gateway;
  gateway.id = vaultrdp::core::Uuid::v4();
  gateway.name = name.trimmed();
  gateway.host = host.trimmed();
  gateway.port = port;
  if (folderId.has_value() && !folderId->trimmed().isEmpty()) {
    gateway.folderId = folderId.value();
  }
  gateway.allowAnyFolder = allowAnyFolder;
  gateway.credentialMode = credentialMode;
  if (credentialId.has_value() && !credentialId->trimmed().isEmpty()) {
    gateway.credentialId = credentialId;
  }
  gateway.createdAt = QDateTime::currentSecsSinceEpoch();

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "INSERT INTO gateways (id, name, host, port, folder_id, allow_any_folder, credential_mode, credential_id, "
      "created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
  query.addBindValue(gateway.id);
  query.addBindValue(gateway.name);
  query.addBindValue(gateway.host);
  query.addBindValue(gateway.port);
  query.addBindValue(sql::nullableString(gateway.folderId));
  query.addBindValue(gateway.allowAnyFolder ? 1 : 0);
  query.addBindValue(static_cast<int>(gateway.credentialMode));
  query.addBindValue(sql::nullableString(gateway.credentialId));
  query.addBindValue(gateway.createdAt);

  if (!sql::execOrLog(query, "Failed to create gateway:")) {
    return std::nullopt;
  }
  return gateway;
}

std::optional<vaultrdp::model::Gateway> GatewayRepository::duplicateGateway(const QString& id) const {
  const auto existing = findGatewayById(id);
  if (!existing.has_value()) {
    return std::nullopt;
  }
  const auto& src = existing.value();
  return createGateway(src.name + " Copy", src.host, src.port, src.credentialMode, src.credentialId, src.folderId,
                       src.allowAnyFolder);
}

std::optional<vaultrdp::model::Gateway> GatewayRepository::findGatewayById(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return std::nullopt;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "SELECT id, name, host, port, folder_id, allow_any_folder, credential_mode, credential_id, created_at "
      "FROM gateways WHERE id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to query gateway by id:")) {
    return std::nullopt;
  }
  if (!query.next()) {
    return std::nullopt;
  }

  return rowmap::gatewayFromQuery(query, 0, 1, 2, 3, 4, 5, 6, 7, 8);
}

bool GatewayRepository::updateGateway(const QString& id, const QString& name, const QString& host, int port,
                                      vaultrdp::model::GatewayCredentialMode credentialMode,
                                      const std::optional<QString>& credentialId,
                                      const std::optional<QString>& folderId,
                                      bool allowAnyFolder) const {
  if (id.trimmed().isEmpty() || name.trimmed().isEmpty() || host.trimmed().isEmpty() || port <= 0 || port > 65535) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "UPDATE gateways SET name = ?, host = ?, port = ?, folder_id = ?, allow_any_folder = ?, "
      "credential_mode = ?, credential_id = ? WHERE id = ?");
  query.addBindValue(name.trimmed());
  query.addBindValue(host.trimmed());
  query.addBindValue(port);
  query.addBindValue(sql::nullableString(folderId));
  query.addBindValue(allowAnyFolder ? 1 : 0);
  query.addBindValue(static_cast<int>(credentialMode));
  query.addBindValue(sql::nullableString(credentialId));
  query.addBindValue(id);

  if (!sql::execOrLog(query, "Failed to update gateway:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool GatewayRepository::moveGatewayToFolder(const QString& id, const std::optional<QString>& folderId) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE gateways SET folder_id = ? WHERE id = ?");
  query.addBindValue(sql::nullableString(folderId));
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to move gateway to folder:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool GatewayRepository::renameGateway(const QString& id, const QString& newName) const {
  if (id.trimmed().isEmpty() || newName.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE gateways SET name = ? WHERE id = ?");
  query.addBindValue(newName.trimmed());
  query.addBindValue(id);

  if (!sql::execOrLog(query, "Failed to rename gateway:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool GatewayRepository::deleteGateway(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("DELETE FROM gateways WHERE id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to delete gateway:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool GatewayRepository::isGatewayInUse(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("SELECT COUNT(1) FROM connections WHERE gateway_id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to check gateway usage:") || !query.next()) {
    return true;
  }
  return query.value(0).toInt() > 0;
}

std::optional<QString> GatewayRepository::findUsernameByGatewayId(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return std::nullopt;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "SELECT c.username "
      "FROM gateways g "
      "LEFT JOIN credentials c ON c.id = g.credential_id "
      "WHERE g.id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to query username for gateway:")) {
    return std::nullopt;
  }
  if (!query.next() || query.value(0).isNull()) {
    return std::nullopt;
  }
  return query.value(0).toString();
}

std::vector<vaultrdp::model::Gateway> GatewayRepository::listGateways() const {
  std::vector<vaultrdp::model::Gateway> gateways;

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
          "SELECT id, name, host, port, folder_id, allow_any_folder, credential_mode, credential_id, created_at "
          "FROM gateways ORDER BY name");
  if (!sql::execOrLog(query, "Failed to query gateways:")) {
    return gateways;
  }

  while (query.next()) {
    gateways.push_back(rowmap::gatewayFromQuery(query, 0, 1, 2, 3, 4, 5, 6, 7, 8));
  }

  return gateways;
}

}  // namespace vaultrdp::core::repository
