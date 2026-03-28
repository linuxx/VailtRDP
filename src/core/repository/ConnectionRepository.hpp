#pragma once

#include <QString>

#include <optional>
#include <vector>

#include "core/model/Entities.hpp"

class DatabaseManager;
namespace vaultrdp::core {
class VaultManager;
}

namespace vaultrdp::core::repository {

struct ConnectionLaunchInfo {
  vaultrdp::model::Connection connection;
  std::optional<QString> username;
  std::optional<QString> domain;
  std::optional<QString> password;
  std::optional<QString> gatewayHost;
  int gatewayPort = 443;
  vaultrdp::model::GatewayCredentialMode gatewayCredentialMode =
      vaultrdp::model::GatewayCredentialMode::SameAsConnection;
  std::optional<QString> gatewayUsername;
  std::optional<QString> gatewayDomain;
  std::optional<QString> gatewayPassword;
  bool gatewayPromptEachTime = false;
};

class ConnectionRepository {
 public:
  explicit ConnectionRepository(DatabaseManager* databaseManager);

  std::optional<vaultrdp::model::Connection> createConnection(
      const QString& name, const QString& host, int port, const std::optional<QString>& folderId,
      const std::optional<QString>& credentialId, const std::optional<QString>& gatewayId = std::nullopt,
      const QString& optionsJson = "{}") const;

  std::optional<vaultrdp::model::Connection> duplicateConnection(const QString& id) const;
  bool updateConnection(const QString& id, const QString& name, const QString& host, int port,
                        const std::optional<QString>& credentialId, const std::optional<QString>& gatewayId,
                        const QString& optionsJson) const;
  bool updateConnectionOptionsJson(const QString& id, const QString& optionsJson) const;
  bool renameConnection(const QString& id, const QString& newName) const;
  bool moveConnectionToFolder(const QString& id, const std::optional<QString>& folderId) const;
  bool deleteConnection(const QString& id) const;
  std::optional<QString> findUsernameByConnectionId(const QString& id) const;
  std::optional<ConnectionLaunchInfo> resolveLaunchInfo(const QString& id, vaultrdp::core::VaultManager* vaultManager) const;
  std::optional<vaultrdp::model::Connection> findConnectionById(const QString& id) const;
  std::vector<vaultrdp::model::Connection> listConnections() const;

 private:
  DatabaseManager* databaseManager_;
};

}  // namespace vaultrdp::core::repository
