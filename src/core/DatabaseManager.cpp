#include "core/DatabaseManager.hpp"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "core/MigrationManager.hpp"

DatabaseManager::DatabaseManager(QString databasePath)
    : databasePath_(std::move(databasePath)),
      connectionName_(QString("vaultrdp-main-%1").arg(QDateTime::currentMSecsSinceEpoch())) {}

DatabaseManager::~DatabaseManager() {
  if (QSqlDatabase::contains(connectionName_)) {
    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    if (db.isOpen()) {
      db.close();
    }
  }
  QSqlDatabase::removeDatabase(connectionName_);
}

bool DatabaseManager::initialize() {
  QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
  database.setDatabaseName(databasePath_);

  if (!database.open()) {
    qCritical() << "Failed to open SQLite database:" << database.lastError().text();
    return false;
  }

  QSqlQuery pragmaQuery(database);
  if (!pragmaQuery.exec("PRAGMA journal_mode=WAL;")) {
    qCritical() << "Failed to set WAL mode:" << pragmaQuery.lastError().text();
    return false;
  }
  pragmaQuery.next();
  pragmaQuery.finish();

  MigrationManager migrationManager;
  if (!migrationManager.applyInitialSchema(database)) {
    return false;
  }

  return true;
}

QSqlDatabase DatabaseManager::database() const {
  return QSqlDatabase::database(connectionName_);
}

QString DatabaseManager::databasePath() const {
  return databasePath_;
}
