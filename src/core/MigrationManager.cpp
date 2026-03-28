#include "core/MigrationManager.hpp"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {

constexpr const char* kInitialSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS folders (
  id TEXT PRIMARY KEY,
  parent_id TEXT NULL,
  name TEXT NOT NULL,
  sort_order INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS connections (
  id TEXT PRIMARY KEY,
  folder_id TEXT,
  name TEXT NOT NULL,
  protocol INTEGER NOT NULL,
  host TEXT NOT NULL,
  port INTEGER NOT NULL,
  gateway_id TEXT NULL,
  credential_id TEXT NULL,
  username TEXT NULL,
  domain TEXT NULL,
  secret_id TEXT NULL,
  resolution TEXT,
  color_depth INTEGER,
  options_json TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  last_connected_at INTEGER,
  FOREIGN KEY(folder_id) REFERENCES folders(id),
  FOREIGN KEY(gateway_id) REFERENCES gateways(id),
  FOREIGN KEY(credential_id) REFERENCES credentials(id),
  FOREIGN KEY(secret_id) REFERENCES secrets(id)
);

CREATE TABLE IF NOT EXISTS gateways (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  host TEXT NOT NULL,
  port INTEGER NOT NULL,
  folder_id TEXT NULL,
  allow_any_folder INTEGER NOT NULL DEFAULT 0,
  credential_mode INTEGER NOT NULL,
  credential_id TEXT NULL,
  created_at INTEGER NOT NULL,
  FOREIGN KEY(folder_id) REFERENCES folders(id),
  FOREIGN KEY(credential_id) REFERENCES credentials(id)
);

CREATE TABLE IF NOT EXISTS credentials (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  folder_id TEXT NULL,
  allow_any_folder INTEGER NOT NULL DEFAULT 0,
  username TEXT NOT NULL,
  domain TEXT NULL,
  secret_id TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  FOREIGN KEY(folder_id) REFERENCES folders(id),
  FOREIGN KEY(secret_id) REFERENCES secrets(id)
);

CREATE TABLE IF NOT EXISTS secrets (
  id TEXT PRIMARY KEY,
  type INTEGER NOT NULL,
  enc_blob BLOB NOT NULL,
  kdf_profile INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS crypto_meta (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  kdf_salt BLOB,
  kdf_memlimit INTEGER,
  kdf_opslimit INTEGER,
  kdf_alg INTEGER,
  schema_version INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS vault_meta (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  verifier_blob BLOB
);

INSERT OR IGNORE INTO crypto_meta (
  id,
  kdf_salt,
  kdf_memlimit,
  kdf_opslimit,
  kdf_alg,
  schema_version
) VALUES (
  1,
  NULL,
  NULL,
  NULL,
  NULL,
  1
);

INSERT OR IGNORE INTO vault_meta (
  id,
  verifier_blob
) VALUES (
  1,
  NULL
);
)SQL";

}  // namespace

bool MigrationManager::applyInitialSchema(QSqlDatabase& database) const {
  QSqlQuery query(database);

  if (!database.transaction()) {
    qCritical() << "Failed to start migration transaction:" << database.lastError().text();
    return false;
  }

  const QString schemaSql = QString::fromUtf8(kInitialSchemaSql);
  const QStringList statements = schemaSql.split(';', Qt::SkipEmptyParts);

  for (QString statement : statements) {
    statement = statement.trimmed();
    if (statement.isEmpty()) {
      continue;
    }

    if (!query.exec(statement)) {
      qCritical() << "Failed to apply schema statement:" << query.lastError().text();
      qCritical() << "Statement:" << statement;
      database.rollback();
      return false;
    }
  }

  auto ensureGatewayColumn = [&](const QString& columnSql) -> bool {
    QSqlQuery alter(database);
    if (!alter.exec(columnSql)) {
      qCritical() << "Failed to migrate gateways table:" << alter.lastError().text();
      qCritical() << "Statement:" << columnSql;
      return false;
    }
    return true;
  };
  auto ensureCredentialColumn = [&](const QString& columnSql) -> bool {
    QSqlQuery alter(database);
    if (!alter.exec(columnSql)) {
      qCritical() << "Failed to migrate credentials table:" << alter.lastError().text();
      qCritical() << "Statement:" << columnSql;
      return false;
    }
    return true;
  };

  QSqlQuery tableInfo(database);
  if (!tableInfo.exec("PRAGMA table_info(gateways)")) {
    qCritical() << "Failed to inspect gateways schema:" << tableInfo.lastError().text();
    database.rollback();
    return false;
  }

  bool hasFolderId = false;
  bool hasAllowAnyFolder = false;
  while (tableInfo.next()) {
    const QString name = tableInfo.value(1).toString();
    if (name == "folder_id") {
      hasFolderId = true;
    } else if (name == "allow_any_folder") {
      hasAllowAnyFolder = true;
    }
  }

  if (!hasFolderId && !ensureGatewayColumn("ALTER TABLE gateways ADD COLUMN folder_id TEXT NULL")) {
    database.rollback();
    return false;
  }
  if (!hasAllowAnyFolder &&
      !ensureGatewayColumn("ALTER TABLE gateways ADD COLUMN allow_any_folder INTEGER NOT NULL DEFAULT 0")) {
    database.rollback();
    return false;
  }

  QSqlQuery credentialTableInfo(database);
  if (!credentialTableInfo.exec("PRAGMA table_info(credentials)")) {
    qCritical() << "Failed to inspect credentials schema:" << credentialTableInfo.lastError().text();
    database.rollback();
    return false;
  }
  bool hasCredentialFolderId = false;
  bool hasCredentialAllowAnyFolder = false;
  while (credentialTableInfo.next()) {
    const QString name = credentialTableInfo.value(1).toString();
    if (name == "folder_id") {
      hasCredentialFolderId = true;
    } else if (name == "allow_any_folder") {
      hasCredentialAllowAnyFolder = true;
    }
  }
  if (!hasCredentialFolderId &&
      !ensureCredentialColumn("ALTER TABLE credentials ADD COLUMN folder_id TEXT NULL")) {
    database.rollback();
    return false;
  }
  if (!hasCredentialAllowAnyFolder &&
      !ensureCredentialColumn("ALTER TABLE credentials ADD COLUMN allow_any_folder INTEGER NOT NULL DEFAULT 0")) {
    database.rollback();
    return false;
  }

  auto ensureConnectionColumn = [&](const QString& columnSql) -> bool {
    QSqlQuery alter(database);
    if (!alter.exec(columnSql)) {
      qCritical() << "Failed to migrate connections table:" << alter.lastError().text();
      qCritical() << "Statement:" << columnSql;
      return false;
    }
    return true;
  };

  QSqlQuery connectionTableInfo(database);
  if (!connectionTableInfo.exec("PRAGMA table_info(connections)")) {
    qCritical() << "Failed to inspect connections schema:" << connectionTableInfo.lastError().text();
    database.rollback();
    return false;
  }
  bool hasConnectionUsername = false;
  bool hasConnectionDomain = false;
  bool hasConnectionSecretId = false;
  while (connectionTableInfo.next()) {
    const QString name = connectionTableInfo.value(1).toString();
    if (name == "username") {
      hasConnectionUsername = true;
    } else if (name == "domain") {
      hasConnectionDomain = true;
    } else if (name == "secret_id") {
      hasConnectionSecretId = true;
    }
  }
  if (!hasConnectionUsername &&
      !ensureConnectionColumn("ALTER TABLE connections ADD COLUMN username TEXT NULL")) {
    database.rollback();
    return false;
  }
  if (!hasConnectionDomain &&
      !ensureConnectionColumn("ALTER TABLE connections ADD COLUMN domain TEXT NULL")) {
    database.rollback();
    return false;
  }
  if (!hasConnectionSecretId &&
      !ensureConnectionColumn("ALTER TABLE connections ADD COLUMN secret_id TEXT NULL")) {
    database.rollback();
    return false;
  }

  if (!database.commit()) {
    qCritical() << "Failed to commit migration transaction:" << database.lastError().text();
    database.rollback();
    return false;
  }

  return true;
}
