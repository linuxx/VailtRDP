#include "core/repository/SecretRepository.hpp"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "core/DatabaseManager.hpp"
#include "core/Uuid.hpp"
#include "core/VaultManager.hpp"

namespace {
constexpr int kSecretTypePassword = 1;
constexpr int kKdfProfileDefault = 1;
}  // namespace

namespace vaultrdp::core::repository {

SecretRepository::SecretRepository(DatabaseManager* databaseManager) : databaseManager_(databaseManager) {}

std::optional<QString> SecretRepository::createPasswordSecret(const QString& plaintextPassword,
                                                              VaultManager* vaultManager) const {
  if (vaultManager == nullptr || plaintextPassword.isEmpty()) {
    return std::nullopt;
  }

  const QByteArray passwordUtf8 = plaintextPassword.toUtf8();
  const std::optional<QByteArray> maybeBlob = vaultManager->encryptSecret(passwordUtf8);
  if (!maybeBlob.has_value()) {
    qWarning() << "Unable to encrypt secret. Vault likely locked.";
    return std::nullopt;
  }

  const QString id = vaultrdp::core::Uuid::v4();
  const qint64 now = QDateTime::currentSecsSinceEpoch();

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "INSERT INTO secrets (id, type, enc_blob, kdf_profile, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)");
  query.addBindValue(id);
  query.addBindValue(kSecretTypePassword);
  query.addBindValue(maybeBlob.value());
  query.addBindValue(kKdfProfileDefault);
  query.addBindValue(now);
  query.addBindValue(now);

  if (!query.exec()) {
    qCritical() << "Failed to insert secret:" << query.lastError().text();
    return std::nullopt;
  }

  return id;
}

std::optional<QString> SecretRepository::decryptPasswordSecret(const QString& secretId,
                                                               VaultManager* vaultManager) const {
  if (vaultManager == nullptr || secretId.trimmed().isEmpty()) {
    return std::nullopt;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("SELECT enc_blob FROM secrets WHERE id = ? AND type = ?");
  query.addBindValue(secretId);
  query.addBindValue(kSecretTypePassword);

  if (!query.exec()) {
    qCritical() << "Failed to query secret by id:" << query.lastError().text();
    return std::nullopt;
  }

  if (!query.next()) {
    return std::nullopt;
  }

  const QByteArray blob = query.value(0).toByteArray();
  const std::optional<QByteArray> maybePlain = vaultManager->decryptSecret(blob);
  if (!maybePlain.has_value()) {
    return std::nullopt;
  }

  return QString::fromUtf8(maybePlain.value());
}

bool SecretRepository::updatePasswordSecret(const QString& secretId, const QString& plaintextPassword,
                                            VaultManager* vaultManager) const {
  if (vaultManager == nullptr || secretId.trimmed().isEmpty() || plaintextPassword.isEmpty()) {
    return false;
  }

  const QByteArray passwordUtf8 = plaintextPassword.toUtf8();
  const std::optional<QByteArray> maybeBlob = vaultManager->encryptSecret(passwordUtf8);
  if (!maybeBlob.has_value()) {
    qWarning() << "Unable to encrypt secret for update. Vault likely locked.";
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE secrets SET enc_blob = ?, updated_at = ? WHERE id = ? AND type = ?");
  query.addBindValue(maybeBlob.value());
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  query.addBindValue(secretId);
  query.addBindValue(kSecretTypePassword);
  if (!query.exec()) {
    qCritical() << "Failed to update secret:" << query.lastError().text();
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool SecretRepository::deleteSecret(const QString& secretId) const {
  if (secretId.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("DELETE FROM secrets WHERE id = ?");
  query.addBindValue(secretId);
  if (!query.exec()) {
    qCritical() << "Failed to delete secret:" << query.lastError().text();
    return false;
  }
  return query.numRowsAffected() > 0;
}

}  // namespace vaultrdp::core::repository
