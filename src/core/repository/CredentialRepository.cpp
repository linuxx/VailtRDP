#include "core/repository/CredentialRepository.hpp"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "core/DatabaseManager.hpp"
#include "core/repository/RowMappers.hpp"
#include "core/repository/SqlHelpers.hpp"
#include "core/Uuid.hpp"

namespace vaultrdp::core::repository {

CredentialRepository::CredentialRepository(DatabaseManager* databaseManager) : databaseManager_(databaseManager) {}

std::optional<vaultrdp::model::Credential> CredentialRepository::createCredential(
    const QString& name, const QString& username, const std::optional<QString>& domain,
    const QString& secretId, const std::optional<QString>& folderId, bool allowAnyFolder) const {
  if (name.trimmed().isEmpty() || username.trimmed().isEmpty() || secretId.trimmed().isEmpty()) {
    return std::nullopt;
  }

  vaultrdp::model::Credential credential;
  credential.id = vaultrdp::core::Uuid::v4();
  credential.name = name.trimmed();
  credential.folderId = folderId;
  credential.allowAnyFolder = allowAnyFolder;
  credential.username = username.trimmed();
  credential.domain = domain;
  credential.secretId = secretId;
  credential.createdAt = QDateTime::currentSecsSinceEpoch();

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "INSERT INTO credentials (id, name, folder_id, allow_any_folder, username, domain, secret_id, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
  query.addBindValue(credential.id);
  query.addBindValue(credential.name);
  QVariant folderIdValue;
  if (credential.folderId.has_value() && !credential.folderId->trimmed().isEmpty()) {
    folderIdValue = credential.folderId.value();
  }
  query.addBindValue(folderIdValue);
  query.addBindValue(credential.allowAnyFolder ? 1 : 0);
  query.addBindValue(credential.username);

  QVariant domainValue;
  if (credential.domain.has_value()) {
    domainValue = credential.domain.value();
  }
  query.addBindValue(domainValue);

  query.addBindValue(credential.secretId);
  query.addBindValue(credential.createdAt);

  if (!sql::execOrLog(query, "Failed to insert credential:")) {
    return std::nullopt;
  }

  return credential;
}

std::optional<vaultrdp::model::Credential> CredentialRepository::duplicateCredential(const QString& id) const {
  const auto existing = findCredentialById(id);
  if (!existing.has_value()) {
    return std::nullopt;
  }
  const auto& src = existing.value();
  return createCredential(src.name + " Copy", src.username, src.domain, src.secretId, src.folderId,
                          src.allowAnyFolder);
}

std::optional<vaultrdp::model::Credential> CredentialRepository::findCredentialById(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return std::nullopt;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "SELECT id, name, folder_id, allow_any_folder, username, domain, secret_id, created_at "
      "FROM credentials WHERE id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to query credential by id:")) {
    return std::nullopt;
  }
  if (!query.next()) {
    return std::nullopt;
  }

  return rowmap::credentialFromQuery(query, 0, 1, 2, 3, 4, 5, 6, 7);
}

bool CredentialRepository::updateCredential(const QString& id, const QString& name, const QString& username,
                                            const std::optional<QString>& domain,
                                            const std::optional<QString>& folderId,
                                            bool allowAnyFolder) const {
  if (id.trimmed().isEmpty() || name.trimmed().isEmpty() || username.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "UPDATE credentials SET name = ?, folder_id = ?, allow_any_folder = ?, username = ?, domain = ? WHERE id = ?");
  query.addBindValue(name.trimmed());
  query.addBindValue(sql::nullableString(folderId));
  query.addBindValue(allowAnyFolder ? 1 : 0);
  query.addBindValue(username.trimmed());

  QVariant domainValue;
  if (domain.has_value() && !domain->trimmed().isEmpty()) {
    domainValue = domain.value();
  }
  query.addBindValue(domainValue);
  query.addBindValue(id);

  if (!sql::execOrLog(query, "Failed to update credential:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool CredentialRepository::renameCredential(const QString& id, const QString& newName) const {
  if (id.trimmed().isEmpty() || newName.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE credentials SET name = ? WHERE id = ?");
  query.addBindValue(newName.trimmed());
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to rename credential:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool CredentialRepository::moveCredentialToFolder(const QString& id, const std::optional<QString>& folderId) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE credentials SET folder_id = ? WHERE id = ?");
  query.addBindValue(sql::nullableString(folderId));
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to move credential to folder:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool CredentialRepository::deleteCredential(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("DELETE FROM credentials WHERE id = ?");
  query.addBindValue(id);
  if (!sql::execOrLog(query, "Failed to delete credential:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

int CredentialRepository::countCredentialReferences(const QString& id) const {
  if (id.trimmed().isEmpty()) {
    return 0;
  }

  QSqlDatabase db = databaseManager_->database();
  int total = 0;

  QSqlQuery connQuery(db);
  connQuery.prepare("SELECT COUNT(1) FROM connections WHERE credential_id = ?");
  connQuery.addBindValue(id);
  if (!sql::execOrLog(connQuery, "Failed to count connection credential references:") || !connQuery.next()) {
    return 0;
  }
  total += connQuery.value(0).toInt();

  QSqlQuery gwQuery(db);
  gwQuery.prepare("SELECT COUNT(1) FROM gateways WHERE credential_id = ?");
  gwQuery.addBindValue(id);
  if (!sql::execOrLog(gwQuery, "Failed to count gateway credential references:") || !gwQuery.next()) {
    return total;
  }
  total += gwQuery.value(0).toInt();
  return total;
}

std::vector<vaultrdp::model::Credential> CredentialRepository::listCredentials() const {
  std::vector<vaultrdp::model::Credential> credentials;

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  if (!query.exec(
          "SELECT id, name, folder_id, allow_any_folder, username, domain, secret_id, created_at "
          "FROM credentials ORDER BY name")) {
    qCritical() << "Failed to query credentials:" << query.lastError().text();
    return credentials;
  }

  while (query.next()) {
    credentials.push_back(rowmap::credentialFromQuery(query, 0, 1, 2, 3, 4, 5, 6, 7));
  }

  return credentials;
}

}  // namespace vaultrdp::core::repository
