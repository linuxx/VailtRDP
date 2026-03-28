#pragma once

#include <QString>

#include <optional>
#include <vector>

#include "core/model/Entities.hpp"

class DatabaseManager;

namespace vaultrdp::core::repository {

class GatewayRepository {
 public:
  explicit GatewayRepository(DatabaseManager* databaseManager);

  std::optional<vaultrdp::model::Gateway> createGateway(
      const QString& name, const QString& host, int port,
      vaultrdp::model::GatewayCredentialMode credentialMode = vaultrdp::model::GatewayCredentialMode::SameAsConnection,
      const std::optional<QString>& credentialId = std::nullopt,
      const std::optional<QString>& folderId = std::nullopt,
      bool allowAnyFolder = false) const;
  std::optional<vaultrdp::model::Gateway> duplicateGateway(const QString& id) const;
  std::optional<vaultrdp::model::Gateway> findGatewayById(const QString& id) const;
  bool updateGateway(const QString& id, const QString& name, const QString& host, int port,
                     vaultrdp::model::GatewayCredentialMode credentialMode,
                     const std::optional<QString>& credentialId,
                     const std::optional<QString>& folderId,
                     bool allowAnyFolder) const;
  bool moveGatewayToFolder(const QString& id, const std::optional<QString>& folderId) const;
  bool renameGateway(const QString& id, const QString& newName) const;
  bool deleteGateway(const QString& id) const;
  bool isGatewayInUse(const QString& id) const;
  std::optional<QString> findUsernameByGatewayId(const QString& id) const;
  std::vector<vaultrdp::model::Gateway> listGateways() const;

 private:
  DatabaseManager* databaseManager_;
};

}  // namespace vaultrdp::core::repository
