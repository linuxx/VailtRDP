#pragma once

#include <QString>

#include <optional>
#include <vector>

#include "core/model/Entities.hpp"

class DatabaseManager;

namespace vaultrdp::core::repository {

class CredentialRepository {
 public:
  explicit CredentialRepository(DatabaseManager* databaseManager);

  std::optional<vaultrdp::model::Credential> createCredential(const QString& name, const QString& username,
                                                              const std::optional<QString>& domain,
                                                              const QString& secretId,
                                                              const std::optional<QString>& folderId = std::nullopt,
                                                              bool allowAnyFolder = false) const;
  std::optional<vaultrdp::model::Credential> duplicateCredential(const QString& id) const;
  std::optional<vaultrdp::model::Credential> findCredentialById(const QString& id) const;
  bool updateCredential(const QString& id, const QString& name, const QString& username,
                        const std::optional<QString>& domain,
                        const std::optional<QString>& folderId = std::nullopt,
                        bool allowAnyFolder = false) const;
  bool renameCredential(const QString& id, const QString& newName) const;
  bool moveCredentialToFolder(const QString& id, const std::optional<QString>& folderId) const;
  bool deleteCredential(const QString& id) const;
  int countCredentialReferences(const QString& id) const;

  std::vector<vaultrdp::model::Credential> listCredentials() const;

 private:
  DatabaseManager* databaseManager_;
};

}  // namespace vaultrdp::core::repository
