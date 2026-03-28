#pragma once

#include <QString>

#include <optional>

namespace vaultrdp::model {

enum class Protocol : int {
  Rdp = 1,
  Ssh = 2,
};

enum class GatewayCredentialMode : int {
  SameAsConnection = 0,
  SeparateSaved = 1,
  PromptEachTime = 2,
};

struct Folder {
  QString id;
  std::optional<QString> parentId;
  QString name;
  int sortOrder = 0;
};

struct Connection {
  QString id;
  QString folderId;
  QString name;
  Protocol protocol = Protocol::Rdp;
  QString host;
  int port = 3389;
  std::optional<QString> gatewayId;
  std::optional<QString> credentialId;
  std::optional<QString> username;
  std::optional<QString> domain;
  std::optional<QString> secretId;
  std::optional<QString> resolution;
  std::optional<int> colorDepth;
  QString optionsJson;
  qint64 createdAt = 0;
  qint64 updatedAt = 0;
  std::optional<qint64> lastConnectedAt;
};

struct Gateway {
  QString id;
  QString name;
  QString host;
  int port = 443;
  std::optional<QString> folderId;
  bool allowAnyFolder = false;
  GatewayCredentialMode credentialMode = GatewayCredentialMode::SameAsConnection;
  std::optional<QString> credentialId;
  qint64 createdAt = 0;
};

struct Credential {
  QString id;
  QString name;
  std::optional<QString> folderId;
  bool allowAnyFolder = false;
  QString username;
  std::optional<QString> domain;
  QString secretId;
  qint64 createdAt = 0;
};

struct Secret {
  QString id;
  int type = 0;
  QByteArray encBlob;
  int kdfProfile = 0;
  qint64 createdAt = 0;
  qint64 updatedAt = 0;
};

}  // namespace vaultrdp::model
