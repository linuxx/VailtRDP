#include "core/repository/ConnectionRepository.hpp"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "core/DatabaseManager.hpp"
#include "core/repository/RowMappers.hpp"
#include "core/Uuid.hpp"
#include "core/VaultManager.hpp"
#include "core/repository/SqlHelpers.hpp"

namespace vaultrdp::core::repository {

namespace {

bool connectionPromptsEveryTime(const QString& optionsJson) {
  const QByteArray json = optionsJson.toUtf8();
  if (json.trimmed().isEmpty()) {
    return false;
  }

  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }

  const QJsonValue promptValue = document.object().value("promptEveryTime");
  return promptValue.isBool() && promptValue.toBool();
}

}  // namespace

ConnectionRepository::ConnectionRepository(DatabaseManager* databaseManager) : databaseManager_(databaseManager) {}

std::optional<vaultrdp::model::Connection> ConnectionRepository::createConnection(
    const QString& name, const QString& host, int port, const std::optional<QString>& folderId,
    const std::optional<QString>& credentialId, const std::optional<QString>& username,
    const std::optional<QString>& domain, const std::optional<QString>& secretId,
    const std::optional<QString>& gatewayId,
    const QString& optionsJson) const {
  if (name.trimmed().isEmpty() || host.trimmed().isEmpty() || port <= 0 || port > 65535) {
    return std::nullopt;
  }

  vaultrdp::model::Connection connection;
  connection.id = vaultrdp::core::Uuid::v4();
  connection.name = name.trimmed();
  connection.host = host.trimmed();
  connection.port = port;
  if (folderId.has_value() && !folderId->trimmed().isEmpty()) {
    connection.folderId = folderId.value();
  }
  connection.protocol = vaultrdp::model::Protocol::Rdp;
  connection.credentialId = credentialId;
  connection.username = username;
  connection.domain = domain;
  connection.secretId = secretId;
  connection.gatewayId = gatewayId;
  connection.optionsJson = optionsJson.trimmed().isEmpty() ? "{}" : optionsJson;
  connection.createdAt = QDateTime::currentSecsSinceEpoch();
  connection.updatedAt = connection.createdAt;

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "INSERT INTO connections (id, folder_id, name, protocol, host, port, gateway_id, credential_id, username, "
      "domain, secret_id, resolution, color_depth, options_json, created_at, updated_at, last_connected_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  query.addBindValue(connection.id);
  query.addBindValue(sql::nullableString(connection.folderId));

  query.addBindValue(connection.name);
  query.addBindValue(static_cast<int>(connection.protocol));
  query.addBindValue(connection.host);
  query.addBindValue(connection.port);
  query.addBindValue(sql::nullableString(connection.gatewayId));
  query.addBindValue(sql::nullableString(connection.credentialId));
  query.addBindValue(sql::nullableString(connection.username));
  query.addBindValue(sql::nullableString(connection.domain));
  query.addBindValue(sql::nullableString(connection.secretId));

  query.addBindValue(QVariant());
  query.addBindValue(QVariant());
  query.addBindValue(connection.optionsJson);
  query.addBindValue(connection.createdAt);
  query.addBindValue(connection.updatedAt);
  query.addBindValue(QVariant());

  if (!sql::execOrLog(query, "Failed to insert connection:")) {
    return std::nullopt;
  }

  return connection;
}

std::optional<vaultrdp::model::Connection> ConnectionRepository::duplicateConnection(const QString& id) const {
  const auto existing = findConnectionById(id);
  if (!existing.has_value()) {
    return std::nullopt;
  }

  const vaultrdp::model::Connection& src = existing.value();
  const QString duplicatedName = src.name + " Copy";
  return createConnection(duplicatedName, src.host, src.port,
                          src.folderId.isEmpty() ? std::nullopt : std::optional<QString>(src.folderId),
                          src.credentialId, src.username, src.domain, src.secretId, src.gatewayId, src.optionsJson);
}

bool ConnectionRepository::updateConnection(const QString& id, const QString& name, const QString& host, int port,
                                            const std::optional<QString>& credentialId,
                                            const std::optional<QString>& username,
                                            const std::optional<QString>& domain,
                                            const std::optional<QString>& secretId,
                                            const std::optional<QString>& gatewayId,
                                            const QString& optionsJson) const {
  if (id.trimmed().isEmpty() || name.trimmed().isEmpty() || host.trimmed().isEmpty() || port <= 0 || port > 65535) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "UPDATE connections SET name = ?, host = ?, port = ?, credential_id = ?, username = ?, domain = ?, "
      "secret_id = ?, gateway_id = ?, "
      "options_json = ?, updated_at = ? WHERE id = ?");
  query.addBindValue(name.trimmed());
  query.addBindValue(host.trimmed());
  query.addBindValue(port);
  query.addBindValue(sql::nullableString(credentialId));
  query.addBindValue(sql::nullableString(username));
  query.addBindValue(sql::nullableString(domain));
  query.addBindValue(sql::nullableString(secretId));
  query.addBindValue(sql::nullableString(gatewayId));
  query.addBindValue(optionsJson.trimmed().isEmpty() ? QString("{}") : optionsJson);
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  query.addBindValue(id);

  if (!sql::execOrLog(query, "Failed to update connection:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool ConnectionRepository::updateConnectionOptionsJson(const QString& id, const QString& optionsJson) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE connections SET options_json = ?, updated_at = ? WHERE id = ?");
  query.addBindValue(optionsJson.trimmed().isEmpty() ? QString("{}") : optionsJson);
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to update connection options_json:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool ConnectionRepository::renameConnection(const QString& id, const QString& newName) const {
  if (id.trimmed().isEmpty() || newName.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE connections SET name = ?, updated_at = ? WHERE id = ?");
  query.addBindValue(newName.trimmed());
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  query.addBindValue(id);

  if (!sql::execOrLog(query, "Failed to rename connection:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool ConnectionRepository::moveConnectionToFolder(const QString& id, const std::optional<QString>& folderId) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE connections SET folder_id = ?, updated_at = ? WHERE id = ?");

  query.addBindValue(sql::nullableString(folderId));
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  query.addBindValue(id);

  if (!sql::execOrLog(query, "Failed to move connection to folder:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool ConnectionRepository::deleteConnection(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("SELECT secret_id FROM connections WHERE id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to query connection secret before delete:")) {
    return false;
  }
  std::optional<QString> secretId;
  if (query.next()) {
    secretId = sql::optionalString(query.value(0));
  }

  if (!db.transaction()) {
    qCritical() << "Failed to start connection delete transaction:" << db.lastError().text();
    return false;
  }

  query.prepare("DELETE FROM connections WHERE id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to delete connection:")) {
    db.rollback();
    return false;
  }
  if (query.numRowsAffected() <= 0) {
    db.rollback();
    return false;
  }

  if (secretId.has_value()) {
    QSqlQuery deleteSecret(db);
    deleteSecret.prepare("DELETE FROM secrets WHERE id = ?");
    deleteSecret.addBindValue(secretId.value());
    if (!sql::execOrLog(deleteSecret, "Failed to delete connection-owned secret:")) {
      db.rollback();
      return false;
    }
  }

  if (!db.commit()) {
    qCritical() << "Failed to commit connection delete transaction:" << db.lastError().text();
    db.rollback();
    return false;
  }
  return true;
}

std::optional<QString> ConnectionRepository::findUsernameByConnectionId(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return std::nullopt;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "SELECT COALESCE(conn.username, c.username) "
      "FROM connections conn "
      "LEFT JOIN credentials c ON c.id = conn.credential_id "
      "WHERE conn.id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to query username for connection:")) {
    return std::nullopt;
  }

  if (!query.next() || query.value(0).isNull()) {
    return std::nullopt;
  }

  return query.value(0).toString();
}

std::optional<ConnectionLaunchInfo> ConnectionRepository::resolveLaunchInfo(
    const QString& id, vaultrdp::core::VaultManager* vaultManager) const {
  const auto connection = findConnectionById(id);
  if (!connection.has_value()) {
    return std::nullopt;
  }

  ConnectionLaunchInfo info;
  info.connection = connection.value();
  info.promptEveryTime = connectionPromptsEveryTime(connection->optionsJson);

  QSqlDatabase db = databaseManager_->database();
  if (connection->credentialId.has_value() || connection->secretId.has_value() || connection->username.has_value() ||
      connection->domain.has_value()) {
    QSqlQuery query(db);
    query.prepare(
        "SELECT conn.username, conn.domain, s_local.enc_blob, c.username, c.domain, s_shared.enc_blob "
        "FROM connections conn "
        "LEFT JOIN secrets s_local ON s_local.id = conn.secret_id "
        "LEFT JOIN credentials c ON c.id = conn.credential_id "
        "LEFT JOIN secrets s_shared ON s_shared.id = c.secret_id "
        "WHERE conn.id = ?");
    query.addBindValue(connection->id);

    if (!sql::execOrLog(query, "Failed to resolve connection credentials:")) {
      return info;
    }

    if (query.next()) {
      if (!query.value(0).isNull()) {
        info.username = query.value(0).toString();
      } else if (!query.value(3).isNull()) {
        info.username = query.value(3).toString();
      }
      if (!query.value(1).isNull()) {
        info.domain = query.value(1).toString();
      } else if (!query.value(4).isNull()) {
        info.domain = query.value(4).toString();
      }

      const QVariant localBlob = query.value(2);
      const QVariant sharedBlob = query.value(5);
      const QVariant chosenBlob = !localBlob.isNull() ? localBlob : sharedBlob;
      if (!chosenBlob.isNull() && vaultManager != nullptr &&
          vaultManager->state() != vaultrdp::core::VaultState::Locked) {
        const QByteArray blob = chosenBlob.toByteArray();
        const auto maybePassword = vaultManager->decryptSecret(blob);
        if (maybePassword.has_value()) {
          info.password = QString::fromUtf8(maybePassword.value());
        }
      }
    }
  }

  if (connection->gatewayId.has_value() && !connection->gatewayId->trimmed().isEmpty()) {
    QSqlQuery gwQuery(db);
    gwQuery.prepare(
        "SELECT g.host, g.port, g.credential_mode, g.credential_id, c.username, c.domain, s.enc_blob "
        "FROM gateways g "
        "LEFT JOIN credentials c ON c.id = g.credential_id "
        "LEFT JOIN secrets s ON s.id = c.secret_id "
        "WHERE g.id = ?");
    gwQuery.addBindValue(connection->gatewayId.value());

    if (!sql::execOrLog(gwQuery, "Failed to resolve gateway settings:")) {
      return std::nullopt;
    }
    if (!gwQuery.next()) {
      qCritical() << "Gateway not found for connection:" << connection->gatewayId.value();
      return std::nullopt;
    }

    if (!gwQuery.value(0).isNull()) {
      const QString gatewayHost = gwQuery.value(0).toString().trimmed();
      if (!gatewayHost.isEmpty()) {
        info.gatewayHost = gatewayHost;
      }
    }
    if (!gwQuery.value(1).isNull()) {
      const int port = gwQuery.value(1).toInt();
      if (port > 0 && port <= 65535) {
        info.gatewayPort = port;
      }
    }
    info.gatewayCredentialMode =
        static_cast<vaultrdp::model::GatewayCredentialMode>(gwQuery.value(2).toInt());

    if (info.gatewayCredentialMode == vaultrdp::model::GatewayCredentialMode::SameAsConnection) {
      info.gatewayUsername = info.username;
      info.gatewayDomain = info.domain;
      info.gatewayPassword = info.password;
    } else if (info.gatewayCredentialMode == vaultrdp::model::GatewayCredentialMode::SeparateSaved) {
      if (!gwQuery.value(4).isNull()) {
        info.gatewayUsername = gwQuery.value(4).toString();
      }
      if (!gwQuery.value(5).isNull()) {
        info.gatewayDomain = gwQuery.value(5).toString();
      }
      if (!gwQuery.value(6).isNull() && vaultManager != nullptr &&
          vaultManager->state() != vaultrdp::core::VaultState::Locked) {
        const QByteArray blob = gwQuery.value(6).toByteArray();
        const auto maybePassword = vaultManager->decryptSecret(blob);
        if (maybePassword.has_value()) {
          info.gatewayPassword = QString::fromUtf8(maybePassword.value());
        }
      }
    } else if (info.gatewayCredentialMode == vaultrdp::model::GatewayCredentialMode::PromptEachTime) {
      info.gatewayPromptEachTime = true;
    }
  }

  return info;
}

std::optional<vaultrdp::model::Connection> ConnectionRepository::findConnectionById(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return std::nullopt;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "SELECT id, folder_id, name, protocol, host, port, gateway_id, credential_id, username, domain, secret_id, "
      "resolution, color_depth, options_json, created_at, updated_at, last_connected_at FROM connections WHERE id = ?");
  query.addBindValue(id);

  if (!sql::execOrLog(query, "Failed to query connection by id:")) {
    return std::nullopt;
  }

  if (!query.next()) {
    return std::nullopt;
  }

  return rowmap::connectionFromQuery(query, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
}

std::vector<vaultrdp::model::Connection> ConnectionRepository::listConnections() const {
  std::vector<vaultrdp::model::Connection> connections;

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "SELECT id, folder_id, name, protocol, host, port, gateway_id, credential_id, username, domain, secret_id, "
      "resolution, color_depth, options_json, created_at, updated_at, last_connected_at FROM connections ORDER BY name");
  if (!sql::execOrLog(query, "Failed to query connections:")) {
    return connections;
  }

  while (query.next()) {
    connections.push_back(rowmap::connectionFromQuery(query, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                                      16));
  }

  return connections;
}

}  // namespace vaultrdp::core::repository
