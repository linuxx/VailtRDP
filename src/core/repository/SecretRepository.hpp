#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

class DatabaseManager;

namespace vaultrdp::core {
class VaultManager;
}

namespace vaultrdp::core::repository {

class SecretRepository {
 public:
  explicit SecretRepository(DatabaseManager* databaseManager);

  std::optional<QString> createPasswordSecret(const QString& plaintextPassword, VaultManager* vaultManager) const;
  std::optional<QString> decryptPasswordSecret(const QString& secretId, VaultManager* vaultManager) const;
  bool updatePasswordSecret(const QString& secretId, const QString& plaintextPassword, VaultManager* vaultManager) const;
  bool deleteSecret(const QString& secretId) const;

 private:
  DatabaseManager* databaseManager_;
};

}  // namespace vaultrdp::core::repository
