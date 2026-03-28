#pragma once

#include <QSqlQuery>

#include "core/model/Entities.hpp"
#include "core/repository/SqlHelpers.hpp"

namespace vaultrdp::core::repository::rowmap {

inline vaultrdp::model::Folder folderFromQuery(QSqlQuery& query, int idCol, int parentIdCol, int nameCol,
                                                int sortOrderCol) {
  vaultrdp::model::Folder folder;
  folder.id = query.value(idCol).toString();
  folder.parentId = sql::optionalString(query.value(parentIdCol));
  folder.name = query.value(nameCol).toString();
  folder.sortOrder = query.value(sortOrderCol).toInt();
  return folder;
}

inline vaultrdp::model::Gateway gatewayFromQuery(QSqlQuery& query, int idCol, int nameCol, int hostCol, int portCol,
                                                 int folderIdCol, int allowAnyFolderCol, int credentialModeCol,
                                                 int credentialIdCol, int createdAtCol) {
  vaultrdp::model::Gateway gateway;
  gateway.id = query.value(idCol).toString();
  gateway.name = query.value(nameCol).toString();
  gateway.host = query.value(hostCol).toString();
  gateway.port = query.value(portCol).toInt();
  gateway.folderId = sql::optionalString(query.value(folderIdCol));
  gateway.allowAnyFolder = query.value(allowAnyFolderCol).toInt() != 0;
  gateway.credentialMode = static_cast<vaultrdp::model::GatewayCredentialMode>(query.value(credentialModeCol).toInt());
  gateway.credentialId = sql::optionalString(query.value(credentialIdCol));
  gateway.createdAt = query.value(createdAtCol).toLongLong();
  return gateway;
}

inline vaultrdp::model::Connection connectionFromQuery(QSqlQuery& query, int idCol, int folderIdCol, int nameCol,
                                                       int protocolCol, int hostCol, int portCol, int gatewayIdCol,
                                                       int credentialIdCol, int resolutionCol, int colorDepthCol,
                                                       int optionsCol, int createdAtCol, int updatedAtCol,
                                                       int lastConnectedAtCol) {
  vaultrdp::model::Connection connection;
  connection.id = query.value(idCol).toString();
  connection.folderId = sql::optionalString(query.value(folderIdCol)).value_or(QString());
  connection.name = query.value(nameCol).toString();
  connection.protocol = static_cast<vaultrdp::model::Protocol>(query.value(protocolCol).toInt());
  connection.host = query.value(hostCol).toString();
  connection.port = query.value(portCol).toInt();
  connection.gatewayId = sql::optionalString(query.value(gatewayIdCol));
  connection.credentialId = sql::optionalString(query.value(credentialIdCol));
  if (!query.value(resolutionCol).isNull()) {
    connection.resolution = query.value(resolutionCol).toString();
  }
  if (!query.value(colorDepthCol).isNull()) {
    connection.colorDepth = query.value(colorDepthCol).toInt();
  }
  connection.optionsJson = query.value(optionsCol).toString();
  connection.createdAt = query.value(createdAtCol).toLongLong();
  connection.updatedAt = query.value(updatedAtCol).toLongLong();
  if (!query.value(lastConnectedAtCol).isNull()) {
    connection.lastConnectedAt = query.value(lastConnectedAtCol).toLongLong();
  }
  return connection;
}

inline vaultrdp::model::Credential credentialFromQuery(QSqlQuery& query, int idCol, int nameCol, int folderIdCol,
                                                       int allowAnyFolderCol, int usernameCol, int domainCol,
                                                       int secretIdCol, int createdAtCol) {
  vaultrdp::model::Credential credential;
  credential.id = query.value(idCol).toString();
  credential.name = query.value(nameCol).toString();
  credential.folderId = sql::optionalString(query.value(folderIdCol));
  credential.allowAnyFolder = query.value(allowAnyFolderCol).toInt() != 0;
  credential.username = query.value(usernameCol).toString();
  credential.domain = sql::optionalString(query.value(domainCol));
  credential.secretId = query.value(secretIdCol).toString();
  credential.createdAt = query.value(createdAtCol).toLongLong();
  return credential;
}

}  // namespace vaultrdp::core::repository::rowmap
