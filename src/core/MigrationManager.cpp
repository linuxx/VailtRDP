#include "core/MigrationManager.hpp"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {

constexpr int kSchemaVersionInitial = 1;
constexpr int kSchemaVersionScopedFolders = 2;
constexpr int kSchemaVersionConnectionOwnedCredentials = 3;
constexpr int kCurrentSchemaVersion = kSchemaVersionConnectionOwnedCredentials;

QString initialSchemaSql() {
  return QStringLiteral(R"SQL(
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
  resolution TEXT,
  color_depth INTEGER,
  options_json TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  last_connected_at INTEGER,
  FOREIGN KEY(folder_id) REFERENCES folders(id),
  FOREIGN KEY(gateway_id) REFERENCES gateways(id),
  FOREIGN KEY(credential_id) REFERENCES credentials(id)
);

CREATE TABLE IF NOT EXISTS gateways (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  host TEXT NOT NULL,
  port INTEGER NOT NULL,
  credential_mode INTEGER NOT NULL,
  credential_id TEXT NULL,
  created_at INTEGER NOT NULL,
  FOREIGN KEY(credential_id) REFERENCES credentials(id)
);

CREATE TABLE IF NOT EXISTS credentials (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  username TEXT NOT NULL,
  domain TEXT NULL,
  secret_id TEXT NOT NULL,
  created_at INTEGER NOT NULL,
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
  %1
);

INSERT OR IGNORE INTO vault_meta (
  id,
  verifier_blob
) VALUES (
  1,
  NULL
);
)SQL").arg(kSchemaVersionInitial);
}

bool execStatements(QSqlDatabase& database, const QString& sql, const QString& errorPrefix) {
  QSqlQuery query(database);
  const QStringList statements = sql.split(';', Qt::SkipEmptyParts);
  for (QString statement : statements) {
    statement = statement.trimmed();
    if (statement.isEmpty()) {
      continue;
    }
    if (!query.exec(statement)) {
      qCritical() << errorPrefix << query.lastError().text();
      qCritical() << "Statement:" << statement;
      return false;
    }
  }
  return true;
}

bool tableHasColumn(QSqlDatabase& database, const QString& tableName, const QString& columnName) {
  QSqlQuery query(database);
  if (!query.exec(QString("PRAGMA table_info(%1)").arg(tableName))) {
    qCritical() << "Failed to inspect" << tableName << "schema:" << query.lastError().text();
    return false;
  }

  while (query.next()) {
    if (query.value(1).toString() == columnName) {
      return true;
    }
  }
  return false;
}

bool ensureColumnExists(QSqlDatabase& database, const QString& tableName, const QString& columnName,
                        const QString& columnSql) {
  const bool hasColumn = tableHasColumn(database, tableName, columnName);
  if (hasColumn) {
    return true;
  }

  QSqlQuery alter(database);
  if (!alter.exec(columnSql)) {
    qCritical() << "Failed to migrate" << tableName << "table:" << alter.lastError().text();
    qCritical() << "Statement:" << columnSql;
    return false;
  }
  return true;
}

std::optional<int> readSchemaVersion(QSqlDatabase& database) {
  QSqlQuery query(database);
  if (!query.exec("SELECT schema_version FROM crypto_meta WHERE id = 1")) {
    qCritical() << "Failed to read schema version:" << query.lastError().text();
    return std::nullopt;
  }
  if (!query.next()) {
    qCritical() << "crypto_meta row missing while reading schema version";
    return std::nullopt;
  }
  return query.value(0).toInt();
}

bool writeSchemaVersion(QSqlDatabase& database, int version) {
  QSqlQuery query(database);
  query.prepare("UPDATE crypto_meta SET schema_version = ? WHERE id = 1");
  query.addBindValue(version);
  if (!query.exec()) {
    qCritical() << "Failed to update schema version:" << query.lastError().text();
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool applyMigration2(QSqlDatabase& database) {
  return ensureColumnExists(database, "gateways", "folder_id", "ALTER TABLE gateways ADD COLUMN folder_id TEXT NULL") &&
         ensureColumnExists(database, "gateways", "allow_any_folder",
                            "ALTER TABLE gateways ADD COLUMN allow_any_folder INTEGER NOT NULL DEFAULT 0") &&
         ensureColumnExists(database, "credentials", "folder_id",
                            "ALTER TABLE credentials ADD COLUMN folder_id TEXT NULL") &&
         ensureColumnExists(database, "credentials", "allow_any_folder",
                            "ALTER TABLE credentials ADD COLUMN allow_any_folder INTEGER NOT NULL DEFAULT 0");
}

bool applyMigration3(QSqlDatabase& database) {
  return ensureColumnExists(database, "connections", "username",
                            "ALTER TABLE connections ADD COLUMN username TEXT NULL") &&
         ensureColumnExists(database, "connections", "domain",
                            "ALTER TABLE connections ADD COLUMN domain TEXT NULL") &&
         ensureColumnExists(database, "connections", "secret_id",
                            "ALTER TABLE connections ADD COLUMN secret_id TEXT NULL");
}

}  // namespace

bool MigrationManager::applyInitialSchema(QSqlDatabase& database) const {
  if (!database.transaction()) {
    qCritical() << "Failed to start migration transaction:" << database.lastError().text();
    return false;
  }

  const QString schemaSql = initialSchemaSql();
  if (!execStatements(database, schemaSql, "Failed to apply schema statement:")) {
    database.rollback();
    return false;
  }

  auto maybeSchemaVersion = readSchemaVersion(database);
  if (!maybeSchemaVersion.has_value()) {
    database.rollback();
    return false;
  }

  int schemaVersion = maybeSchemaVersion.value();
  if (schemaVersion < kSchemaVersionScopedFolders) {
    if (!applyMigration2(database) || !writeSchemaVersion(database, kSchemaVersionScopedFolders)) {
      database.rollback();
      return false;
    }
    schemaVersion = kSchemaVersionScopedFolders;
  }

  if (schemaVersion < kSchemaVersionConnectionOwnedCredentials) {
    if (!applyMigration3(database) || !writeSchemaVersion(database, kSchemaVersionConnectionOwnedCredentials)) {
      database.rollback();
      return false;
    }
    schemaVersion = kSchemaVersionConnectionOwnedCredentials;
  }

  if (schemaVersion != kCurrentSchemaVersion && !writeSchemaVersion(database, kCurrentSchemaVersion)) {
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
